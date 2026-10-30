#include "worker.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "constants.h"

#include "core/constants.h"
#include "util/log.h"

namespace gpu {

namespace {

// Loop count that should take `target_ms` given that `count` took
// `elapsed_ms` (kernel time is linear in the loop count).
int Extrapolate(int count, double elapsed_ms, double target_ms) {
  if (elapsed_ms <= 0.0) {
    return kGpuKernelLoopBaseMax;
  }
  const double est = static_cast<double>(count) * target_ms / elapsed_ms;
  return static_cast<int>(std::clamp(est, 1.0, static_cast<double>(kGpuKernelLoopBaseMax)));
}

}  // namespace

GpuWorkerContext::GpuWorkerContext(int id, std::unique_ptr<GpuDevice> device)
    : id_(id),
      device_(std::move(device)),
      base_loop_count_(1),
      calibrated_(false),
      load_set_(0),
      yield_(false),
      memory_bytes_(0),
      memory_held_(false),
      memory_failed_(false) {}

GpuWorkerContext::~GpuWorkerContext() {
  // jthread joins in its dtor; nothing extra to do.
}

std::optional<double> GpuWorkerContext::TimeKernelMs(int loop_count) {
  const auto start = std::chrono::steady_clock::now();
  if (!device_->RunKernel(loop_count)) {
    return std::nullopt;
  }
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
      .count();
}

std::optional<double> GpuWorkerContext::MedianKernelMs(int loop_count) {
  double samples[3];
  for (double &s : samples) {
    const auto t = TimeKernelMs(loop_count);
    if (!t) {
      return std::nullopt;
    }
    s = *t;
  }
  std::sort(std::begin(samples), std::end(samples));
  return samples[1];
}

bool GpuWorkerContext::CalibrateBaseLoopCount() {
  if (device_ == nullptr) {
    return false;
  }
  const double target_ms = static_cast<double>(kScheduleIntervalMS);
  // Warm-up launch: the first one pays for JIT / lazy context setup.
  if (!device_->RunKernel(1)) {
    LOG_WARN("gpu worker id=%d: kernel launch failed during calibration", id_);
    return false;
  }
  // Grow the loop count until one launch is long enough to time reliably,
  // then extrapolate linearly and refine on medians of three. This
  // replaces a per-backend doubling + bisection that timed single samples
  // with whole-millisecond resolution and capped out at 50M iterations
  // (under the target on top-end GPUs).
  int count = 1024;
  std::optional<double> elapsed;
  while (true) {
    elapsed = TimeKernelMs(count);
    if (!elapsed) {
      LOG_WARN("gpu worker id=%d: kernel launch failed during calibration", id_);
      return false;
    }
    if (*elapsed >= kGpuCalibrationMinSampleMs || count >= kGpuKernelLoopBaseMax) {
      break;
    }
    count = static_cast<int>(std::min<int64_t>(int64_t{count} * 4, kGpuKernelLoopBaseMax));
  }
  for (int round = 0; round < kGpuCalibrationRefineRounds; ++round) {
    const int next = Extrapolate(count, *elapsed, target_ms);
    elapsed = MedianKernelMs(next);
    if (!elapsed) {
      LOG_WARN("gpu worker id=%d: kernel launch failed during calibration", id_);
      return false;
    }
    count = next;
    if (std::abs(*elapsed - target_ms) <= target_ms * kGpuCalibrationTolerance) {
      break;
    }
  }
  if (count >= kGpuKernelLoopBaseMax && *elapsed < target_ms) {
    LOG_WARN("gpu worker id=%d: device too fast to fill %d ms even at the loop cap", id_,
             kScheduleIntervalMS);
  }
  base_loop_count_ = std::max(1, count);
  calibrated_ = true;
  LOG_DEBUG("gpu worker id=%d base_loop_count=%d (%.1f ms per launch, target %d ms)", id_,
            base_loop_count_, *elapsed, kScheduleIntervalMS);
  return true;
}

void GpuWorkerContext::Start() {
  thread_ = std::jthread([this](std::stop_token stoken) { Loop(stoken); });
}

void GpuWorkerContext::RequestStop() { thread_.request_stop(); }

void GpuWorkerContext::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void GpuWorkerContext::AdaptBaseLoopCount(int target_ms, double elapsed_ms) {
  if (target_ms < kGpuAdaptMinBusyMs || elapsed_ms <= 0.0) {
    return;
  }
  const double ratio = static_cast<double>(target_ms) / elapsed_ms;
  if (std::abs(ratio - 1.0) < kGpuAdaptDeadband) {
    return;
  }
  const double next =
      static_cast<double>(base_loop_count_) * (1.0 + kGpuAdaptSmoothing * (ratio - 1.0));
  const int new_count =
      static_cast<int>(std::clamp(next, 1.0, static_cast<double>(kGpuKernelLoopBaseMax)));
  LOG_TRACE("gpu worker id=%d: %.1f ms for a %d ms budget, base_loop_count %d -> %d", id_,
            elapsed_ms, target_ms, base_loop_count_, new_count);
  base_loop_count_ = new_count;
}

void GpuWorkerContext::SyncMemory(bool yield) {
  if (device_ == nullptr || memory_bytes_ == 0) {
    return;
  }
  if (yield) {
    if (memory_held_) {
      device_->ReleaseMemory();
      memory_held_ = false;
    }
    memory_failed_ = false;
    return;
  }
  if (memory_held_ || memory_failed_) {
    return;
  }
  memory_held_ = device_->AllocateMemory(memory_bytes_);
  if (!memory_held_) {
    memory_failed_ = true;
    LOG_WARN("gpu worker id=%d: memory allocation failed (%zu bytes); continuing", id_,
             memory_bytes_);
  }
}

void GpuWorkerContext::Loop(std::stop_token stoken) {
  const auto period = std::chrono::milliseconds(kScheduleIntervalMS);
  int consecutive_failures = 0;
  while (!stoken.stop_requested()) {
    const bool yield = yield_.load(std::memory_order_relaxed);
    // Before taking the tick's start time: filling a large block takes a
    // while and must not count as kernel time in AdaptBaseLoopCount().
    SyncMemory(yield);
    const auto start = std::chrono::steady_clock::now();
    const int target_ms = yield ? 0 : load_set_.load(std::memory_order_relaxed);

    if (target_ms > 0 && device_ != nullptr && !calibrated_) {
      // Yielding since startup: calibrate now that the device is ours.
      if (!CalibrateBaseLoopCount()) {
        LOG_ERROR("gpu worker id=%d: kernel calibration failed, giving the device up", id_);
        SyncMemory(true);
        return;
      }
      continue;  // a fresh tick: calibration time isn't this tick's work
    }
    if (target_ms > 0 && device_ != nullptr) {
      // base_loop_count_ corresponds to kScheduleIntervalMS of work.
      const int64_t scaled =
          static_cast<int64_t>(base_loop_count_) * target_ms / kScheduleIntervalMS;
      const int loop_count = static_cast<int>(std::max<int64_t>(scaled, 1));
      if (!device_->RunKernel(loop_count)) {
        if (++consecutive_failures >= kGpuMaxConsecutiveFailures) {
          LOG_ERROR("gpu worker id=%d: %d consecutive kernel failures, giving the device up", id_,
                    consecutive_failures);
          // Nobody follows the yield state from here on: don't leave the
          // `-gm` block pinned on a device we no longer drive.
          SyncMemory(true);
          return;
        }
        LOG_WARN("gpu worker id=%d kernel launch failed; backing off", id_);
        std::this_thread::sleep_for(period);
        continue;
      }
      consecutive_failures = 0;
      // Follow clock boost / thermal throttling, as the CPU workers do.
      AdaptBaseLoopCount(target_ms, std::chrono::duration<double, std::milli>(
                                        std::chrono::steady_clock::now() - start)
                                        .count());
    }

    const auto now = std::chrono::steady_clock::now();
    if (now < start + period) {
      std::this_thread::sleep_until(start + period);
    }
  }
  LOG_TRACE("gpu worker id=%d stopped", id_);
}

}  // namespace gpu
