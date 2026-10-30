#include "worker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

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

GpuWorkerContext::GpuWorkerContext(int id, std::unique_ptr<GpuDevice> device,
                                   std::uint32_t traffic_seed)
    : id_(id),
      device_(std::move(device)),
      base_loop_count_(1),
      calibrated_(false),
      load_set_(0),
      others_percent_(-1),
      busy_us_(0),
      memory_bytes_(0),
      memory_held_(false),
      memory_failed_(false),
      traffic_envelope_(traffic_seed),
      mem_ms_per_mib_(0.0),
      traffic_bytes_per_second_(0.0) {}

GpuWorkerContext::~GpuWorkerContext() {
  // jthread joins in its dtor; nothing extra to do.
}

std::optional<double> GpuWorkerContext::TimeKernelMs(int loop_count) {
  const auto start = std::chrono::steady_clock::now();
  if (!device_->RunKernel(loop_count, 0)) {
    return std::nullopt;
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;
  busy_us_.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count(),
                     std::memory_order_relaxed);
  return std::chrono::duration<double, std::milli>(elapsed).count();
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
  if (!device_->RunKernel(1, 0)) {
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

void GpuWorkerContext::AdaptBaseLoopCount(double alu_target_ms, double alu_elapsed_ms) {
  if (alu_target_ms < kGpuAdaptMinBusyMs || alu_elapsed_ms <= 0.0) {
    return;
  }
  const double ratio = alu_target_ms / alu_elapsed_ms;
  if (std::abs(ratio - 1.0) < kGpuAdaptDeadband) {
    return;
  }
  const double next =
      static_cast<double>(base_loop_count_) * (1.0 + kGpuAdaptSmoothing * (ratio - 1.0));
  const int new_count =
      static_cast<int>(std::clamp(next, 1.0, static_cast<double>(kGpuKernelLoopBaseMax)));
  LOG_TRACE("gpu worker id=%d: ALU loop %.1f ms for %.1f ms, base_loop_count %d -> %d", id_,
            alu_elapsed_ms, alu_target_ms, base_loop_count_, new_count);
  base_loop_count_ = new_count;
}

void GpuWorkerContext::UpdateMemCost(double mib, double residual_ms) {
  // Wall time = ALU loop + mib * cost + a fixed launch overhead: with the
  // ALU loop's share taken out, the cost is the slope of the residual over
  // the traffic and the overhead its intercept. A fit over the recent
  // launches uses the whole spread of the traffic's random walk, where the
  // difference between two launches would drown in a few ms of timing
  // noise (a sleep overshooting, a launch queued behind another); older
  // launches fade out so the fit follows the device.
  MemCostFit &f = mem_cost_fit_;
  const double keep = 1.0 - kGpuMemCostForget;
  f.w = f.w * keep + 1.0;
  f.x = f.x * keep + mib;
  f.y = f.y * keep + residual_ms;
  f.xx = f.xx * keep + mib * mib;
  f.xy = f.xy * keep + mib * residual_ms;
  f.launches = std::min(f.launches + 1, kGpuMemCostMinLaunches);  // no overflow
  if (f.launches < kGpuMemCostMinLaunches) {
    return;
  }
  const double mean_x = f.x / f.w;
  const double var_x = f.xx / f.w - mean_x * mean_x;
  if (var_x < kGpuMemCostMinSpread * kGpuMemCostMinSpread * mean_x * mean_x) {
    return;  // the traffic barely moved: the slope would be noise
  }
  mem_ms_per_mib_ = (f.xy / f.w - mean_x * f.y / f.w) / var_x;
}

void GpuWorkerContext::SyncMemory(bool sharing) {
  if (device_ == nullptr || memory_bytes_ == 0) {
    return;
  }
  if (sharing) {
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
  if (memory_held_ && traffic_bytes_per_second_ <= 0.0) {
    const auto bandwidth = device_->MemoryBandwidth();
    traffic_bytes_per_second_ = bandwidth ? *bandwidth * kGpuMemTrafficBandwidthShare
                                          : static_cast<double>(kGpuMemTrafficMiBPerSecond) *
                                                static_cast<double>(kMebiByte);
    LOG_DEBUG(
        "gpu worker id=%d: memory bandwidth %.1f GB/s (0: unknown), traffic %.0f MiB/s "
        "at full load",
        id_, bandwidth.value_or(0.0) / 1e9,
        traffic_bytes_per_second_ / static_cast<double>(kMebiByte));
  }
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
    const int others = others_percent_.load(std::memory_order_relaxed);
    // Before taking the tick's start time: filling a large block takes a
    // while and must not count as kernel time in AdaptBaseLoopCount().
    SyncMemory(others >= 0);
    const auto start = std::chrono::steady_clock::now();
    // While sharing, the others' share comes out of the budget: device
    // busy time, ours and theirs, adds up to the load asked for.
    const int target_ms =
        std::max(0, load_set_.load(std::memory_order_relaxed) -
                        std::max(others, 0) * kScheduleIntervalMS / kGpuMaxLoadPerDevice);

    if (target_ms > 0 && device_ != nullptr && !calibrated_) {
      // In use by others since startup: calibrate now that there is room.
      if (!CalibrateBaseLoopCount()) {
        LOG_ERROR("gpu worker id=%d: kernel calibration failed, giving the device up", id_);
        SyncMemory(true);
        return;
      }
      continue;  // a fresh tick: calibration time isn't this tick's work
    }
    if (target_ms > 0 && device_ != nullptr) {
      // Memory traffic: on average in proportion to the busy time, scaled
      // by this device's fluctuating factor; see kGpuMemTrafficMiBPerSecond.
      std::uint64_t traffic = 0;
      if (memory_held_) {
        const double stepped_ms =
            traffic_stepped_at_
                ? std::chrono::duration<double, std::milli>(start - *traffic_stepped_at_).count()
                : 0.0;
        traffic_stepped_at_ = start;
        const double mean = traffic_bytes_per_second_ * target_ms / kMillisecondsPerSecond;
        traffic = static_cast<std::uint64_t>(mean * traffic_envelope_.Next(stepped_ms));
      }
      const double mib = static_cast<double>(traffic) / static_cast<double>(kMebiByte);
      // The launch's time is its ALU loop plus what the traffic costs: give
      // the loop only the rest of the budget, so the busy time stays at
      // target_ms however the traffic moves, and AdaptBaseLoopCount() sees
      // the loop alone (clock drift, not the traffic's random walk).
      const double mem_ms = std::clamp(mem_ms_per_mib_ * mib, 0.0, target_ms * kGpuMemCostMaxShare);
      // base_loop_count_ corresponds to kScheduleIntervalMS of work.
      const double scaled = static_cast<double>(base_loop_count_) * (target_ms - mem_ms) /
                            static_cast<double>(kScheduleIntervalMS);
      const int loop_count = static_cast<int>(std::max(scaled, 1.0));
      if (!device_->RunKernel(loop_count, traffic)) {
        if (++consecutive_failures >= kGpuMaxConsecutiveFailures) {
          LOG_ERROR("gpu worker id=%d: %d consecutive kernel failures, giving the device up", id_,
                    consecutive_failures);
          // Nobody follows the sharing state from here on: don't leave the
          // `-gm` block pinned on a device we no longer drive.
          SyncMemory(true);
          return;
        }
        LOG_WARN("gpu worker id=%d kernel launch failed; backing off", id_);
        std::this_thread::sleep_for(period);
        continue;
      }
      consecutive_failures = 0;
      const auto elapsed = std::chrono::steady_clock::now() - start;
      busy_us_.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count(),
                         std::memory_order_relaxed);
      const double elapsed_ms = std::chrono::duration<double, std::milli>(elapsed).count();
      const double alu_ms = static_cast<double>(loop_count) *
                            static_cast<double>(kScheduleIntervalMS) /
                            static_cast<double>(base_loop_count_);
      if (traffic != 0) {
        UpdateMemCost(mib, elapsed_ms - alu_ms);
      }
      // Follow clock boost / thermal throttling, as the CPU workers do.
      AdaptBaseLoopCount(alu_ms, elapsed_ms - std::max(mem_ms_per_mib_, 0.0) * mib);
    }

    const auto now = std::chrono::steady_clock::now();
    if (now < start + period) {
      std::this_thread::sleep_until(start + period);
    }
  }
  LOG_TRACE("gpu worker id=%d stopped", id_);
}

}  // namespace gpu
