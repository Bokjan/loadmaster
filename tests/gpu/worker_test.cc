// Unit tests for gpu::GpuWorkerContext with a fake device.
//
// Calibration used to be duplicated in all five backends (doubling +
// bisection on single, millisecond-resolution samples, capped at 50M
// loops). It now lives in the worker: grow until a launch is long enough
// to time, extrapolate linearly, refine on medians of three. The fake
// device "runs" a kernel by sleeping for loop_count * ns_per_loop, so we
// can check the result against the known answer without a GPU.

#include "gpu/worker.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "core/constants.h"
#include "gpu/constants.h"
#include "util/log.h"

#include <gtest/gtest.h>

namespace {

using gpu::GpuDevice;
using gpu::GpuWorkerContext;

class FakeDevice final : public GpuDevice {
 public:
  // A launch takes loop_count * ns_per_loop, plus ms_per_mib for every MiB
  // of memory traffic.
  explicit FakeDevice(double ns_per_loop, double ms_per_mib = 0.0)
      : ns_per_loop_(ns_per_loop), ms_per_mib_(ms_per_mib) {}

  bool Init(int) override { return true; }
  std::string Name() const override { return "fake"; }
  std::optional<double> MemoryBandwidth() const override { return bandwidth_; }
  bool AllocateMemory(size_t bytes) override {
    ++allocations_;
    held_bytes_ = bytes;
    return true;
  }
  void ReleaseMemory() override {
    ++releases_;
    held_bytes_ = 0;
  }
  bool RunKernel(int loop_count, std::uint64_t mem_traffic_bytes) override {
    if (fail_.load()) {
      ++failures_;
      return false;
    }
    ++launches_;
    last_traffic_bytes_ = mem_traffic_bytes;
    const double ms = loop_count * ns_per_loop_ / 1e6 +
                      ms_per_mib_ * static_cast<double>(mem_traffic_bytes) / kMebiByte;
    if (mem_traffic_bytes != 0) {
      std::lock_guard<std::mutex> lock(traffic_mutex_);
      traffic_.push_back(mem_traffic_bytes);
      traffic_launch_ms_.push_back(ms);
    }
    // Sleeps overshoot, by milliseconds on macOS and Windows: count the
    // time the launch really took, as a GPU's busy counter would.
    const auto start = std::chrono::steady_clock::now();
    const auto duration = std::chrono::nanoseconds(static_cast<int64_t>(ms * 1e6));
    if (spin_) {
      while (std::chrono::steady_clock::now() - start < duration) {
      }
    } else {
      std::this_thread::sleep_for(duration);
    }
    busy_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start)
                    .count();
    return true;
  }

  std::atomic<bool> fail_{false};
  std::atomic<int> launches_{0};
  std::atomic<int> failures_{0};
  std::atomic<int> allocations_{0};
  std::atomic<int> releases_{0};
  std::atomic<size_t> held_bytes_{0};
  std::atomic<std::uint64_t> last_traffic_bytes_{0};
  std::atomic<int64_t> busy_ns_{0};  // wall time of all launches
  std::optional<double> bandwidth_;  // bytes per second; set before Start()
  // Spin instead of sleeping, for launch times exact to well below a ms
  // (and a core busy meanwhile); set before Start().
  bool spin_ = false;
  std::mutex traffic_mutex_;
  std::vector<std::uint64_t> traffic_;     // every non-zero traffic, in order
  std::vector<double> traffic_launch_ms_;  // and how long those launches took

 private:
  double ns_per_loop_;
  double ms_per_mib_;
};

class GpuWorkerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    util::logger_internal::g_default_logger->SetLevel(util::Logger::kLevelOff);
  }
  void TearDown() override {
    util::logger_internal::g_default_logger->SetLevel(util::Logger::kLevelWarn);
  }
};

TEST_F(GpuWorkerTest, CalibrationFindsOnePeriodOfWork) {
  // 2 us per loop -> one 100 ms period is 50'000 loops.
  auto dev = std::make_unique<FakeDevice>(2000.0);
  FakeDevice *raw = dev.get();
  GpuWorkerContext ctx(0, std::move(dev));
  ASSERT_TRUE(ctx.CalibrateBaseLoopCount());
  // Measure what the calibrated count actually costs.
  ctx.SetLoadSet(kScheduleIntervalMS);
  const int before = raw->launches_.load();
  ctx.Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(1050));
  ctx.RequestStop();
  ctx.Join();
  const int launches = raw->launches_.load() - before;
  // A full-load worker runs back to back: ~10 launches per second if the
  // calibration is right (sleep overshoot makes it slightly fewer).
  EXPECT_GE(launches, 8);
  EXPECT_LE(launches, 11);
}

TEST_F(GpuWorkerTest, CalibrationFailsWhenTheKernelCannotRun) {
  auto dev = std::make_unique<FakeDevice>(1000.0);
  dev->fail_ = true;
  GpuWorkerContext ctx(0, std::move(dev));
  EXPECT_FALSE(ctx.CalibrateBaseLoopCount());
}

TEST_F(GpuWorkerTest, WorkerGivesUpAfterConsecutiveFailures) {
  // Succeeds through calibration, then every launch fails.
  auto dev = std::make_unique<FakeDevice>(20000.0);
  FakeDevice *raw = dev.get();
  GpuWorkerContext ctx(0, std::move(dev));
  ASSERT_TRUE(ctx.CalibrateBaseLoopCount());
  raw->fail_ = true;
  ctx.SetLoadSet(1);
  ctx.SetMemoryBytes(4096);
  ctx.Start();
  // kGpuMaxConsecutiveFailures retries, about one per period, then it
  // stops. Poll instead of sleeping a fixed number of periods: sleeps
  // overshoot on loaded or timer-coalescing hosts (a macOS CI runner managed
  // only 43 periods in 6 s).
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (raw->failures_.load() < kGpuMaxConsecutiveFailures &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kScheduleIntervalMS));
  }
  const int failures = raw->failures_.load();
  std::this_thread::sleep_for(std::chrono::milliseconds(3 * kScheduleIntervalMS));
  EXPECT_EQ(raw->failures_.load(), failures) << "worker kept retrying";
  EXPECT_EQ(failures, kGpuMaxConsecutiveFailures);
  EXPECT_EQ(raw->allocations_.load(), 1);
  EXPECT_EQ(raw->held_bytes_.load(), 0u) << "`-gm` block kept after giving up";
  ctx.RequestStop();
  ctx.Join();
}

// Poll `done` every scheduling period until it holds or `seconds` pass.
template <typename Pred>
bool WaitFor(Pred done, int seconds = 10) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  while (!done()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kScheduleIntervalMS / 2));
  }
  return true;
}

TEST_F(GpuWorkerTest, MemoryIsAllocatedByTheWorkerThread) {
  auto dev = std::make_unique<FakeDevice>(2000.0);
  FakeDevice *raw = dev.get();
  GpuWorkerContext ctx(0, std::move(dev));
  ctx.SetMemoryBytes(4096);
  // Memory-only (`-gm` without `-g`): no calibration, no load.
  EXPECT_EQ(raw->allocations_.load(), 0) << "allocated before Start()";
  ctx.Start();
  EXPECT_TRUE(WaitFor([&] { return raw->held_bytes_.load() == 4096; }));
  ctx.RequestStop();
  ctx.Join();
  EXPECT_EQ(raw->launches_.load(), 0);
  EXPECT_EQ(raw->allocations_.load(), 1);
}

TEST_F(GpuWorkerTest, MemoryTrafficFollowsTheLoad) {
  // A seed whose walk starts away from the factor's bounds, so the first
  // launch's traffic isn't clamped and no later one can equal it exactly.
  constexpr std::uint32_t kSeed = 1;
  {
    const double first = gpu::MemTrafficEnvelope(kSeed).Next(0.0);
    ASSERT_GT(first, kGpuMemTrafficMinFactor);
    ASSERT_LT(first, kGpuMemTrafficMaxFactor);
  }
  auto dev = std::make_unique<FakeDevice>(2000.0);
  FakeDevice *raw = dev.get();
  GpuWorkerContext ctx(0, std::move(dev), kSeed);
  ASSERT_TRUE(ctx.CalibrateBaseLoopCount());
  EXPECT_EQ(raw->last_traffic_bytes_.load(), 0u) << "calibration moved memory";
  ctx.SetLoadSet(kScheduleIntervalMS / 2);
  ctx.SetMemoryBytes(4096);
  ctx.Start();
  ASSERT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(raw->traffic_mutex_);
    return raw->traffic_.size() >= 5;
  }));
  ctx.RequestStop();
  ctx.Join();
  // Half the period busy: on average half of kGpuMemTrafficMiBPerSecond,
  // times a factor that differs from launch to launch.
  const double mean = static_cast<double>(kGpuMemTrafficMiBPerSecond) *
                      static_cast<double>(kMebiByte) * (kScheduleIntervalMS / 2) /
                      kMillisecondsPerSecond;
  std::lock_guard<std::mutex> lock(raw->traffic_mutex_);
  for (const std::uint64_t t : raw->traffic_) {
    EXPECT_GE(static_cast<double>(t), mean * kGpuMemTrafficMinFactor * 0.999);
    EXPECT_LE(static_cast<double>(t), mean * kGpuMemTrafficMaxFactor * 1.001);
  }
  EXPECT_NE(raw->traffic_.front(), raw->traffic_.back()) << "traffic doesn't fluctuate";
}

TEST_F(GpuWorkerTest, MemoryTrafficTimeComesOutOfTheAluLoop) {
  // 0.3 ms per MiB: with this seed's factor the traffic costs ~2-10 ms of
  // a 50 ms launch. The worker learns that cost and shortens the ALU loop
  // to match, so launch times don't follow the traffic. Without that,
  // AdaptBaseLoopCount() chases each change a few ticks late and launch
  // time rises and falls with the traffic (slope ~0.25 ms per MiB here).
  // The launches spin: a sleep's overshoot (up to ~15 ms on CI's macOS
  // runners) would swamp the few ms the traffic moves them by.
  constexpr double kMsPerMiB = 0.3;
  auto dev = std::make_unique<FakeDevice>(2000.0, kMsPerMiB);
  FakeDevice *raw = dev.get();
  raw->spin_ = true;
  GpuWorkerContext ctx(0, std::move(dev), 1);
  ASSERT_TRUE(ctx.CalibrateBaseLoopCount());
  ctx.SetLoadSet(kScheduleIntervalMS / 2);
  ctx.SetMemoryBytes(4096);
  ctx.Start();
  constexpr std::size_t kWarmUp = 20;
  constexpr std::size_t kLaunches = kWarmUp + 30;
  ASSERT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(raw->traffic_mutex_);
    return raw->traffic_.size() >= kLaunches;
  }));
  ctx.RequestStop();
  ctx.Join();
  // Least-squares slope of launch time over traffic.
  std::lock_guard<std::mutex> lock(raw->traffic_mutex_);
  double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
  for (std::size_t i = kWarmUp; i < kLaunches; ++i) {
    const double x = static_cast<double>(raw->traffic_[i]) / kMebiByte;
    const double y = raw->traffic_launch_ms_[i];
    sx += x;
    sy += y;
    sxx += x * x;
    sxy += x * y;
  }
  const double n = static_cast<double>(kLaunches - kWarmUp);
  const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
  printf("SLOPE %f\n", slope);
  EXPECT_LT(std::abs(slope), 0.3 * kMsPerMiB) << "launch time follows the traffic";
}

TEST_F(GpuWorkerTest, MemoryTrafficScalesWithTheDevicesBandwidth) {
  // A seed whose walk starts away from the factor's bounds (see above).
  constexpr std::uint32_t kSeed = 1;
  const double first = gpu::MemTrafficEnvelope(kSeed).Next(0.0);
  auto dev = std::make_unique<FakeDevice>(2000.0);
  FakeDevice *raw = dev.get();
  raw->bandwidth_ = 200e9;
  GpuWorkerContext ctx(0, std::move(dev), kSeed);
  ASSERT_TRUE(ctx.CalibrateBaseLoopCount());
  ctx.SetLoadSet(kScheduleIntervalMS / 2);
  ctx.SetMemoryBytes(4096);
  ctx.Start();
  ASSERT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(raw->traffic_mutex_);
    return !raw->traffic_.empty();
  }));
  ctx.RequestStop();
  ctx.Join();
  // The first launch: its share of the bandwidth for half a period, times
  // the walk's starting factor.
  const double expected = 200e9 * kGpuMemTrafficBandwidthShare * (kScheduleIntervalMS / 2) /
                          kMillisecondsPerSecond * first;
  std::lock_guard<std::mutex> lock(raw->traffic_mutex_);
  EXPECT_NEAR(static_cast<double>(raw->traffic_.front()), expected, expected * 1e-6);
}

TEST_F(GpuWorkerTest, NoMemoryTrafficWithoutABlock) {
  auto dev = std::make_unique<FakeDevice>(2000.0);
  FakeDevice *raw = dev.get();
  GpuWorkerContext ctx(0, std::move(dev));
  ASSERT_TRUE(ctx.CalibrateBaseLoopCount());
  ctx.SetLoadSet(kScheduleIntervalMS / 2);
  const int before = raw->launches_.load();
  ctx.Start();
  ASSERT_TRUE(WaitFor([&] { return raw->launches_.load() > before + 2; }));
  ctx.RequestStop();
  ctx.Join();
  EXPECT_EQ(raw->last_traffic_bytes_.load(), 0u);
}

TEST_F(GpuWorkerTest, SharingFillsOnlyTheGapAndFreesMemory) {
  auto dev = std::make_unique<FakeDevice>(2000.0);
  FakeDevice *raw = dev.get();
  GpuWorkerContext ctx(0, std::move(dev));
  ASSERT_TRUE(ctx.CalibrateBaseLoopCount());
  ctx.SetLoadSet(kScheduleIntervalMS * 6 / 10);
  ctx.SetMemoryBytes(4096);
  // Others take 40% of the device: of the 60% asked for, 20% is left.
  ctx.SetOthers(40);
  EXPECT_TRUE(ctx.Sharing());
  ctx.Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(3 * kScheduleIntervalMS));
  const int64_t busy_before = raw->busy_ns_.load();
  const double own_before = ctx.BusyMs();
  std::this_thread::sleep_for(std::chrono::milliseconds(20 * kScheduleIntervalMS));
  const double busy_ms = static_cast<double>(raw->busy_ns_.load() - busy_before) / 1e6;
  const double own_ms = ctx.BusyMs() - own_before;
  ctx.RequestStop();
  ctx.Join();
  EXPECT_NEAR(busy_ms, 20 * kScheduleIntervalMS * 0.2, 20 * kScheduleIntervalMS * 0.05);
  EXPECT_EQ(raw->allocations_.load(), 0) << "held the block while sharing";
  // BusyMs() counts kernel wall time, what the monitor takes out of the
  // device's busy time.
  EXPECT_NEAR(own_ms, busy_ms, 20 * kScheduleIntervalMS * 0.05);
}

TEST_F(GpuWorkerTest, OthersTakingEverythingStopsLaunchesAndFreesMemoryThenResumes) {
  auto dev = std::make_unique<FakeDevice>(2000.0);
  FakeDevice *raw = dev.get();
  GpuWorkerContext ctx(0, std::move(dev));
  ASSERT_TRUE(ctx.CalibrateBaseLoopCount());
  ctx.SetLoadSet(kScheduleIntervalMS / 2);
  ctx.SetMemoryBytes(4096);
  ctx.Start();
  ASSERT_TRUE(
      WaitFor([&] { return raw->held_bytes_.load() == 4096 && raw->launches_.load() > 0; }));

  ctx.SetOthers(100);
  ASSERT_TRUE(WaitFor([&] { return raw->held_bytes_.load() == 0; }));
  // Let a launch that was already running finish, then nothing more.
  std::this_thread::sleep_for(std::chrono::milliseconds(2 * kScheduleIntervalMS));
  const int launches = raw->launches_.load();
  std::this_thread::sleep_for(std::chrono::milliseconds(5 * kScheduleIntervalMS));
  EXPECT_EQ(raw->launches_.load(), launches) << "kept launching with no room left";
  EXPECT_EQ(raw->releases_.load(), 1);

  ctx.SetOthers(std::nullopt);
  EXPECT_TRUE(WaitFor([&] { return raw->held_bytes_.load() == 4096; }));
  EXPECT_TRUE(WaitFor([&] { return raw->launches_.load() > launches; }));
  EXPECT_EQ(raw->allocations_.load(), 2);
  ctx.RequestStop();
  ctx.Join();
}

TEST_F(GpuWorkerTest, SharingFromTheStartNeverAllocates) {
  auto dev = std::make_unique<FakeDevice>(2000.0);
  FakeDevice *raw = dev.get();
  GpuWorkerContext ctx(0, std::move(dev));
  ctx.SetMemoryBytes(4096);
  ctx.SetOthers(0);
  ctx.Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(5 * kScheduleIntervalMS));
  EXPECT_EQ(raw->allocations_.load(), 0);
  ctx.RequestStop();
  ctx.Join();
}

TEST_F(GpuWorkerTest, WorkerSharingAtStartupCalibratesOnceThereIsRoom) {
  // Init() skips calibration on a device another process is using.
  auto dev = std::make_unique<FakeDevice>(2000.0);
  FakeDevice *raw = dev.get();
  GpuWorkerContext ctx(0, std::move(dev));
  ctx.SetLoadSet(kScheduleIntervalMS / 2);
  ctx.SetOthers(100);
  ctx.Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(3 * kScheduleIntervalMS));
  EXPECT_EQ(raw->launches_.load(), 0) << "calibrated on a device in use";

  // They take less than the load asked for: room for some of ours.
  ctx.SetOthers(30);
  // Don't judge by the loop count launched afterwards: AdaptBaseLoopCount()
  // grows an uncalibrated worker's count of 1 to the same place within a
  // few ticks, and where the fake's sleeps overshoot (by tens of ms on the
  // macOS CI runner) it settles far below what 2 us per loop predicts.
  ASSERT_TRUE(WaitFor([&] { return raw->launches_.load() > 0; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(5 * kScheduleIntervalMS));
  ctx.RequestStop();
  ctx.Join();
  EXPECT_TRUE(ctx.calibrated()) << "launched without calibrating";
}

TEST_F(GpuWorkerTest, FailedDeferredCalibrationGivesTheDeviceUp) {
  auto dev = std::make_unique<FakeDevice>(2000.0);
  FakeDevice *raw = dev.get();
  dev->fail_ = true;
  GpuWorkerContext ctx(0, std::move(dev));
  ctx.SetLoadSet(kScheduleIntervalMS / 2);
  ctx.SetMemoryBytes(4096);
  ctx.Start();
  // Holds the block, fails the one calibration launch, frees the block.
  EXPECT_TRUE(WaitFor([&] { return raw->releases_.load() == 1; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(3 * kScheduleIntervalMS));
  EXPECT_EQ(raw->failures_.load(), 1);
  EXPECT_EQ(raw->held_bytes_.load(), 0u);
  ctx.RequestStop();
  ctx.Join();
}

}  // namespace
