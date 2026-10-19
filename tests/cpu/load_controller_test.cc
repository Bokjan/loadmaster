// Unit tests for cpu::LoadController.
//
// The controller is pure, so we close the loop against a small simulated
// system that mimics what the real CPU manager sees each 100 ms tick:
//
//   * other processes contribute `other` load;
//   * our workers deliver `efficiency * demand + overhead` (calibration is
//     never perfect, and the main thread costs something);
//   * the system reading is other + ours + bounded noise (/proc/stat at
//     USER_HZ=100 is coarse over 100 ms);
//   * our own reading (util::ProcStat) refreshes only every 5 ticks;
//   * both readings go through the same rolling mean the manager uses.
//
// What we pin: the system average converges on the target, the demand
// does not ride the [0, capacity] rails (the old deadbeat controller was
// effectively on/off), it absorbs a step in other load, and it tracks
// rand_normal-style target steps instead of idling for seconds.

#include "cpu/load_controller.h"

#include <cstdint>
#include <deque>
#include <vector>

#include "cpu/constants.h"
#include "util/rolling_sampler.h"

#include <gtest/gtest.h>

namespace {

using cpu::LoadController;

struct Step {
  int target;
  double other;
};

struct Tick {
  int target;
  double system;
  int demand;
};

class SimulatedSystem {
 public:
  SimulatedSystem(int capacity, double efficiency, double noise)
      : capacity_(capacity), efficiency_(efficiency), noise_(noise) {}

  std::vector<Tick> Run(const std::vector<Step> &steps) {
    std::vector<Tick> out;
    out.reserve(steps.size());
    LoadController controller;
    util::RollingSampler<double> system_avg(kCpuAvgLoadSamplingCount);
    util::RollingSampler<double> process_avg(kCpuAvgLoadSamplingCount);
    std::deque<double> recent_ours;
    double process_reading = 0.0;
    int demand = 0;
    for (size_t t = 0; t < steps.size(); ++t) {
      const double ours = efficiency_ * demand + 1.0;
      recent_ours.push_back(ours);
      if (recent_ours.size() > 5) {
        recent_ours.pop_front();
      }
      const double system = steps[t].other + ours + Noise();
      if (t % 5 == 0) {
        double sum = 0.0;
        for (double v : recent_ours) {
          sum += v;
        }
        process_reading = sum / static_cast<double>(recent_ours.size());
      }
      system_avg.InsertValue(system);
      process_avg.InsertValue(process_reading);
      demand = controller.Update(steps[t].target, system_avg.GetMean(), process_avg.GetMean(),
                                 capacity_);
      out.push_back({steps[t].target, system, demand});
    }
    return out;
  }

 private:
  // Deterministic LCG noise in [-noise_, +noise_].
  double Noise() {
    state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
    const double unit = static_cast<double>(state_ >> 11) / static_cast<double>(1ULL << 53);
    return (unit * 2.0 - 1.0) * noise_;
  }

  int capacity_;
  double efficiency_;
  double noise_;
  uint64_t state_ = 42;
};

std::vector<Step> Repeat(int ticks, int target, double other) {
  return std::vector<Step>(static_cast<size_t>(ticks), Step{target, other});
}

std::vector<Step> Concat(std::initializer_list<std::vector<Step>> parts) {
  std::vector<Step> out;
  for (const auto &p : parts) {
    out.insert(out.end(), p.begin(), p.end());
  }
  return out;
}

double MeanSystem(const std::vector<Tick> &ticks, size_t begin, size_t end) {
  double sum = 0.0;
  for (size_t i = begin; i < end; ++i) {
    sum += ticks[i].system;
  }
  return sum / static_cast<double>(end - begin);
}

TEST(LoadControllerTest, ZeroCapacityYieldsZero) {
  LoadController c;
  EXPECT_EQ(c.Update(200, 50.0, 0.0, 0), 0);
}

TEST(LoadControllerTest, DemandStaysWithinCapacity) {
  LoadController c;
  for (int i = 0; i < 1000; ++i) {
    const int d = c.Update(400, 0.0, 0.0, 300);  // can never reach target
    EXPECT_GE(d, 0);
    EXPECT_LE(d, 300);
  }
  for (int i = 0; i < 1000; ++i) {
    const int d = c.Update(0, 500.0, 0.0, 300);  // always above target
    EXPECT_GE(d, 0);
    EXPECT_LE(d, 300);
  }
}

TEST(LoadControllerTest, IntegralDoesNotWindUpWhileSaturated) {
  // Saturated high for a long time, then the target becomes reachable:
  // without anti-windup the stored integral would keep the output pinned
  // at capacity long after the error flipped sign.
  LoadController c;
  for (int i = 0; i < 5000; ++i) {
    c.Update(400, 300.0, 300.0, 300);
  }
  EXPECT_LE(c.Integral(), 400.0);
  // Now other load leaves room; system is above the new target.
  const int d = c.Update(100, 150.0, 100.0, 300);
  EXPECT_LT(d, 300);
}

TEST(LoadControllerTest, ConvergesOnTargetWithImperfectWorkers) {
  SimulatedSystem sim(/*capacity=*/400, /*efficiency=*/0.9, /*noise=*/15.0);
  const auto ticks = sim.Run(Repeat(600, 200, 50.0));
  EXPECT_NEAR(MeanSystem(ticks, 300, 600), 200.0, 4.0);
}

TEST(LoadControllerTest, DoesNotRideTheRails) {
  SimulatedSystem sim(400, 0.95, 15.0);
  const auto ticks = sim.Run(Repeat(600, 200, 50.0));
  int at_rail = 0;
  for (size_t i = 50; i < ticks.size(); ++i) {
    if (ticks[i].demand == 0 || ticks[i].demand == 400) {
      ++at_rail;
    }
  }
  EXPECT_EQ(at_rail, 0);
}

TEST(LoadControllerTest, AbsorbsStepInOtherLoad) {
  SimulatedSystem sim(400, 0.95, 15.0);
  const auto ticks = sim.Run(Concat({Repeat(300, 200, 50.0), Repeat(300, 200, 150.0)}));
  // Within ~3 s of another process adding 100 the average is back on target.
  EXPECT_NEAR(MeanSystem(ticks, 330, 600), 200.0, 6.0);
}

TEST(LoadControllerTest, TracksRandNormalStyleTargetSteps) {
  // A 10 s rand_normal point dropping from 300 to 100: the old controller
  // idled at 0 for ~6.7 s and averaged ~33 over that step.
  SimulatedSystem sim(400, 0.95, 15.0);
  const auto ticks =
      sim.Run(Concat({Repeat(300, 200, 50.0), Repeat(100, 300, 50.0), Repeat(100, 100, 50.0)}));
  EXPECT_NEAR(MeanSystem(ticks, 300, 400), 300.0, 15.0);
  EXPECT_NEAR(MeanSystem(ticks, 400, 500), 100.0, 15.0);
}

}  // namespace
