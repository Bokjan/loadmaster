#include "worker.h"

#include <chrono>

#include "core/constants.h"
#include "util/log.h"

namespace gpu {

GpuWorkerContext::GpuWorkerContext(int id, std::unique_ptr<GpuDevice> device)
    : id_(id), device_(std::move(device)), base_loop_count_(1), load_set_(0) {}

GpuWorkerContext::~GpuWorkerContext() {
  // jthread joins in its dtor; nothing extra to do.
}

void GpuWorkerContext::CalibrateBaseLoopCount() {
  if (device_ == nullptr) {
    return;
  }
  base_loop_count_ = device_->ProbeBaseLoopCount(kScheduleIntervalMS);
  if (base_loop_count_ <= 0) {
    base_loop_count_ = 1;
  }
  LOG_DEBUG("gpu worker id=%d base_loop_count=%d", id_, base_loop_count_);
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

void GpuWorkerContext::Loop(std::stop_token stoken) {
  const auto period = std::chrono::milliseconds(kScheduleIntervalMS);
  while (!stoken.stop_requested()) {
    const auto start = std::chrono::high_resolution_clock::now();
    const int target_ms = load_set_.load(std::memory_order_relaxed);

    if (target_ms > 0 && device_ != nullptr) {
      // Estimate how many base-loop "units" of work fit into target_ms.
      // base_loop_count_ corresponds to kScheduleIntervalMS of work.
      const int64_t scaled =
          static_cast<int64_t>(base_loop_count_) * target_ms / kScheduleIntervalMS;
      const int loop_count = static_cast<int>(std::max<int64_t>(scaled, 1));
      if (!device_->RunKernel(loop_count)) {
        LOG_WARN("gpu worker id=%d kernel launch failed; backing off", id_);
        std::this_thread::sleep_for(period);
        continue;
      }
    }

    const auto now = std::chrono::high_resolution_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
    const auto sleep_ms = (elapsed < period) ? (period - elapsed) : std::chrono::milliseconds(0);
    std::this_thread::sleep_for(sleep_ms);
  }
  LOG_TRACE("gpu worker id=%d stopped", id_);
}

}  // namespace gpu
