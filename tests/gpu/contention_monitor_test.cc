// Unit tests for the contention monitor: the per-device yield hysteresis
// and how observations from several sources are merged. Time is injected
// and the sources are fakes, so nothing depends on a GPU or a timer.

#include "gpu/contention/monitor.h"

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "gpu/constants.h"
#include "util/log.h"

#include <gtest/gtest.h>

namespace {

using gpu::DeviceLocator;
using gpu::contention::Monitor;
using gpu::contention::Observation;
using gpu::contention::Source;
using gpu::contention::YieldGate;
using Verdict = Observation::Verdict;

class FakeSource final : public Source {
 public:
  explicit FakeSource(std::vector<Observation> *script) : script_(script) {}
  const char *Name() const override { return "fake"; }
  void Sample(const std::vector<DeviceLocator> &devices, std::vector<Observation> &out) override {
    for (size_t i = 0; i < devices.size() && i < script_->size(); ++i) {
      out[i] = (*script_)[i];
    }
  }

 private:
  std::vector<Observation> *script_;
};

class ContentionMonitorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    util::logger_internal::g_default_logger->SetLevel(util::Logger::kLevelOff);
  }
  void TearDown() override {
    util::logger_internal::g_default_logger->SetLevel(util::Logger::kLevelWarn);
  }
};

const TimePoint kT0 = TimePoint{} + std::chrono::hours(1);

TimePoint At(int ms) { return kT0 + std::chrono::milliseconds(ms); }

TEST_F(ContentionMonitorTest, GateYieldsAtOnceAndResumesAfterTheHoldTime) {
  YieldGate gate;
  EXPECT_FALSE(gate.Update(false, At(0)));
  EXPECT_TRUE(gate.Update(true, At(1000)));
  EXPECT_TRUE(gate.Update(false, At(1000 + kGpuYieldResumeMS - 1)));
  // Seen again: the hold time restarts from here.
  EXPECT_TRUE(gate.Update(true, At(2000)));
  EXPECT_TRUE(gate.Update(false, At(2000 + kGpuYieldResumeMS - 1)));
  EXPECT_FALSE(gate.Update(false, At(2000 + kGpuYieldResumeMS)));
}

TEST_F(ContentionMonitorTest, AnyBusySourceWinsAndChangesAreReported) {
  std::vector<Observation> nvml(2);
  std::vector<Observation> drm(2);
  std::vector<std::unique_ptr<Source>> sources;
  sources.push_back(std::make_unique<FakeSource>(&nvml));
  sources.push_back(std::make_unique<FakeSource>(&drm));
  std::vector<std::tuple<size_t, bool, int>> changes;
  Monitor monitor({"gpu0", "gpu1"}, std::vector<DeviceLocator>(2), std::move(sources),
                  [&](size_t i, const std::string &, bool yield, int pid) {
                    changes.emplace_back(i, yield, pid);
                  });

  nvml = {{Verdict::kIdle, 0}, {Verdict::kUnknown, 0}};
  drm = {{Verdict::kUnknown, 0}, {Verdict::kIdle, 0}};
  monitor.Poll(At(0));
  EXPECT_TRUE(changes.empty());

  // One source busy is enough, even if another says idle.
  drm = {{Verdict::kBusy, 42}, {Verdict::kIdle, 0}};
  monitor.Poll(At(2000));
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes[0], std::make_tuple(size_t{0}, true, 42));

  // Still busy: no repeated report.
  monitor.Poll(At(4000));
  EXPECT_EQ(changes.size(), 1u);

  drm = {{Verdict::kIdle, 0}, {Verdict::kIdle, 0}};
  monitor.Poll(At(4000 + kGpuYieldResumeMS - 1));
  EXPECT_EQ(changes.size(), 1u);
  monitor.Poll(At(4000 + kGpuYieldResumeMS));
  ASSERT_EQ(changes.size(), 2u);
  EXPECT_EQ(changes[1], std::make_tuple(size_t{0}, false, 0));
}

TEST_F(ContentionMonitorTest, UnknownDevicesNeverYield) {
  std::vector<std::tuple<size_t, bool, int>> changes;
  Monitor monitor({"gpu0"}, std::vector<DeviceLocator>(1), {},
                  [&](size_t i, const std::string &, bool yield, int pid) {
                    changes.emplace_back(i, yield, pid);
                  });
  monitor.Poll(At(0));
  monitor.Poll(At(2000));
  EXPECT_TRUE(changes.empty());
}

// Records what it was handed, then reports idle everywhere.
class RecordingSource final : public Source {
 public:
  const char *Name() const override { return "recording"; }
  void Sample(const std::vector<DeviceLocator> &, std::vector<Observation> &out) override {
    handed_ = out;
    for (auto &o : out) {
      if (o.verdict != Verdict::kBusy) {
        o.verdict = Verdict::kIdle;
      }
    }
  }
  std::vector<Observation> handed_;
};

TEST_F(ContentionMonitorTest, LaterSourcesAreHandedWhatIsAlreadyBusy) {
  std::vector<Observation> first = {{Verdict::kBusy, 42}, {Verdict::kIdle, 0}};
  auto recording = std::make_unique<RecordingSource>();
  RecordingSource *later = recording.get();
  std::vector<std::unique_ptr<Source>> sources;
  sources.push_back(std::make_unique<FakeSource>(&first));
  sources.push_back(std::move(recording));
  Monitor monitor({"gpu0", "gpu1"}, std::vector<DeviceLocator>(2), std::move(sources), nullptr);
  monitor.Poll(At(0));
  ASSERT_EQ(later->handed_.size(), 2u);
  EXPECT_EQ(later->handed_[0].verdict, Verdict::kBusy);
  EXPECT_EQ(later->handed_[0].pid, 42);
  EXPECT_EQ(later->handed_[1].verdict, Verdict::kUnknown);
}

TEST_F(ContentionMonitorTest, ThreadStopsPromptly) {
  std::vector<Observation> idle(1, Observation{Verdict::kIdle, 0});
  std::vector<std::unique_ptr<Source>> sources;
  sources.push_back(std::make_unique<FakeSource>(&idle));
  Monitor monitor({"gpu0"}, std::vector<DeviceLocator>(1), std::move(sources), nullptr);
  monitor.Start();
  // Mid-wait, not before the thread got going.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const auto start = std::chrono::steady_clock::now();
  monitor.RequestStop();
  monitor.Join();
  // Woken at once, not at the end of the 2 s wait (or a 100 ms sleep step).
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(50));
}

}  // namespace
