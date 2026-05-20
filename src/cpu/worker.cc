#include "worker.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "critical_loop.h"

#include "core/constants.h"
#include "util/log.h"

namespace cpu {

namespace {

// Runtime feedback parameters. The worker re-estimates how long one
// CriticalLoop(base_loop_count_) call really takes, then nudges
// base_loop_count_ back toward "1ms per call" so our busy-time accounting
// stays accurate even when the CPU re-clocks itself.
constexpr double kEmaAlpha = 0.20;         // 0..1; bigger = faster reaction
constexpr double kAdjustDeadband = 0.05;   // ignore drift below 5%
constexpr double kAdjustSmoothing = 0.50;  // 0..1; how much of the new estimate to apply
constexpr double kTargetUsPerCall =
    static_cast<double>(kCpuSchedulingGranularityNS) / 1000.0;  // 1000us by default

}  // namespace

CpuWorkerContext::CpuWorkerContext(int id, int initial_base_loop_count)
    : id_(id),
      load_set_(0),
      base_loop_count_(std::max(1, initial_base_loop_count)),
      ema_us_per_call_(kTargetUsPerCall) {}

CpuWorkerContext::~CpuWorkerContext() {
  // jthread joins in its dtor; nothing extra to do.
}

void CpuWorkerContext::Start() {
  thread_ = std::jthread([this](std::stop_token stoken) { Loop(stoken); });
}

void CpuWorkerContext::RequestStop() { thread_.request_stop(); }

void CpuWorkerContext::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void CpuWorkerContext::UpdateBaseLoopCount(int iterations, int64_t busy_us) {
  if (iterations <= 0 || busy_us <= 0) {
    return;
  }
  const double sample_us = static_cast<double>(busy_us) / iterations;
  ema_us_per_call_ = kEmaAlpha * sample_us + (1.0 - kEmaAlpha) * ema_us_per_call_;

  // Drift relative to the 1ms-per-call target. Skip tiny drift to avoid
  // chasing measurement noise.
  const double drift = std::abs(ema_us_per_call_ - kTargetUsPerCall) / kTargetUsPerCall;
  if (drift < kAdjustDeadband) {
    return;
  }

  // base_loop_count is already calibrated for "1 call = ~kTargetUsPerCall".
  // If observed time is X us per call (with X != kTargetUsPerCall), scale
  // base_loop_count by kTargetUsPerCall / X to bring it back.
  if (ema_us_per_call_ <= 0.0) {
    return;
  }
  const double scale = kTargetUsPerCall / ema_us_per_call_;
  const double target = static_cast<double>(base_loop_count_) * scale;
  // Smooth the actual update to avoid oscillation when there's contention.
  const double next = static_cast<double>(base_loop_count_) +
                      kAdjustSmoothing * (target - static_cast<double>(base_loop_count_));

  int new_count = static_cast<int>(std::lround(next));
  new_count = std::clamp(new_count, 1, kCpuBaseLoopCountMax);
  if (new_count != base_loop_count_) {
    LOG_TRACE("worker id=%d: ema=%.2fus, base_loop_count %d -> %d (drift=%.1f%%)", id_,
              ema_us_per_call_, base_loop_count_, new_count, drift * 100.0);
    base_loop_count_ = new_count;
  }
}

void CpuWorkerContext::Loop(std::stop_token stoken) {
  using clock = std::chrono::high_resolution_clock;
  const auto period = std::chrono::milliseconds(kScheduleIntervalMS);

  [[likely]] while (!stoken.stop_requested()) {
    const auto period_start = clock::now();
    const int target_ms = load_set_.load(std::memory_order_relaxed);
    const auto busy_target = std::chrono::milliseconds(target_ms);

    int iterations = 0;
    if (target_ms > 0) {
      // Burn until the busy-time budget is exhausted. Each CriticalLoop()
      // call is sized to take ~1ms, giving us ~1ms time-check granularity.
      const auto busy_deadline = period_start + busy_target;
      while (clock::now() < busy_deadline) {
        cpu::CriticalLoop(base_loop_count_);
        ++iterations;
      }
    }

    // Feed observed busy time back into base_loop_count_ for the next tick.
    if (iterations > 0) {
      const auto busy_us =
          std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - period_start)
              .count();
      UpdateBaseLoopCount(iterations, busy_us);
    }

    // Sleep the rest of the period.
    const auto now = clock::now();
    const auto deadline = period_start + period;
    if (now < deadline) {
      std::this_thread::sleep_until(deadline);
    }
  }
  LOG_TRACE("cpu worker id=%d stopped", id_);
}

}  // namespace cpu
