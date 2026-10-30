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
#include <memory>
#include <thread>

#include "core/constants.h"
#include "gpu/constants.h"
#include "util/log.h"

#include <gtest/gtest.h>

namespace {

using gpu::GpuDevice;
using gpu::GpuWorkerContext;

class FakeDevice final : public GpuDevice {
 public:
  explicit FakeDevice(double ns_per_loop) : ns_per_loop_(ns_per_loop) {}

  bool Init(int) override { return true; }
  std::string Name() const override { return "fake"; }
  bool AllocateMemory(size_t bytes) override {
    ++allocations_;
    held_bytes_ = bytes;
    return true;
  }
  void ReleaseMemory() override {
    ++releases_;
    held_bytes_ = 0;
  }
  bool RunKernel(int loop_count) override {
    if (fail_.load()) {
      ++failures_;
      return false;
    }
    ++launches_;
    std::this_thread::sleep_for(
        std::chrono::nanoseconds(static_cast<int64_t>(loop_count * ns_per_loop_)));
    return true;
  }

  std::atomic<bool> fail_{false};
  std::atomic<int> launches_{0};
  std::atomic<int> failures_{0};
  std::atomic<int> allocations_{0};
  std::atomic<int> releases_{0};
  std::atomic<size_t> held_bytes_{0};

 private:
  double ns_per_loop_;
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

TEST_F(GpuWorkerTest, YieldStopsLaunchesAndFreesMemoryThenResumes) {
  auto dev = std::make_unique<FakeDevice>(2000.0);
  FakeDevice *raw = dev.get();
  GpuWorkerContext ctx(0, std::move(dev));
  ASSERT_TRUE(ctx.CalibrateBaseLoopCount());
  ctx.SetLoadSet(kScheduleIntervalMS / 2);
  ctx.SetMemoryBytes(4096);
  ctx.Start();
  ASSERT_TRUE(
      WaitFor([&] { return raw->held_bytes_.load() == 4096 && raw->launches_.load() > 0; }));

  ctx.SetYield(true);
  ASSERT_TRUE(WaitFor([&] { return raw->held_bytes_.load() == 0; }));
  // Let a launch that was already running finish, then nothing more.
  std::this_thread::sleep_for(std::chrono::milliseconds(2 * kScheduleIntervalMS));
  const int launches = raw->launches_.load();
  std::this_thread::sleep_for(std::chrono::milliseconds(5 * kScheduleIntervalMS));
  EXPECT_EQ(raw->launches_.load(), launches) << "kept launching while yielding";
  EXPECT_EQ(raw->releases_.load(), 1);

  ctx.SetYield(false);
  EXPECT_TRUE(WaitFor([&] { return raw->held_bytes_.load() == 4096; }));
  EXPECT_TRUE(WaitFor([&] { return raw->launches_.load() > launches; }));
  EXPECT_EQ(raw->allocations_.load(), 2);
  ctx.RequestStop();
  ctx.Join();
}

TEST_F(GpuWorkerTest, YieldingFromTheStartNeverAllocates) {
  auto dev = std::make_unique<FakeDevice>(2000.0);
  FakeDevice *raw = dev.get();
  GpuWorkerContext ctx(0, std::move(dev));
  ctx.SetMemoryBytes(4096);
  ctx.SetYield(true);
  ctx.Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(5 * kScheduleIntervalMS));
  EXPECT_EQ(raw->allocations_.load(), 0);
  ctx.RequestStop();
  ctx.Join();
}

TEST_F(GpuWorkerTest, WorkerYieldingAtStartupCalibratesOnResume) {
  // Init() skips calibration on a device another process is using.
  auto dev = std::make_unique<FakeDevice>(2000.0);
  FakeDevice *raw = dev.get();
  GpuWorkerContext ctx(0, std::move(dev));
  ctx.SetLoadSet(kScheduleIntervalMS / 2);
  ctx.SetYield(true);
  ctx.Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(3 * kScheduleIntervalMS));
  EXPECT_EQ(raw->launches_.load(), 0) << "calibrated on a device in use";

  ctx.SetYield(false);
  // Don't judge by the loop count launched afterwards: AdaptBaseLoopCount()
  // grows an uncalibrated worker's count of 1 to the same place within a
  // few ticks, and where the fake's sleeps overshoot (by tens of ms on the
  // macOS CI runner) it settles far below what 2 us per loop predicts.
  ASSERT_TRUE(WaitFor([&] { return raw->launches_.load() > 0; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(5 * kScheduleIntervalMS));
  ctx.RequestStop();
  ctx.Join();
  EXPECT_TRUE(ctx.calibrated()) << "resumed without calibrating";
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
