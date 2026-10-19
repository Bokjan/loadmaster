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
  bool AllocateMemory(size_t) override { return true; }
  void ReleaseMemory() override {}
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
  ctx.Start();
  // kGpuMaxConsecutiveFailures retries, one per period, then it stops.
  std::this_thread::sleep_for(
      std::chrono::milliseconds((kGpuMaxConsecutiveFailures + 10) * kScheduleIntervalMS));
  const int failures = raw->failures_.load();
  std::this_thread::sleep_for(std::chrono::milliseconds(3 * kScheduleIntervalMS));
  EXPECT_EQ(raw->failures_.load(), failures) << "worker kept retrying";
  EXPECT_EQ(failures, kGpuMaxConsecutiveFailures);
  ctx.RequestStop();
  ctx.Join();
}

}  // namespace
