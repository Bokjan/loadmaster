#include "manager.h"

#include "core/constants.h"
#include "core/options.h"
#include "util/log.h"

namespace gpu {

GpuResourceManager::GpuResourceManager(
    const core::Options &options,
    std::vector<std::pair<int, std::unique_ptr<GpuDevice>>> devices)
    : ResourceManager(options), pending_devices_(std::move(devices)) {}

bool GpuResourceManager::Init() {
  if (pending_devices_.empty()) {
    return false;
  }
  if (options_.GetGpuLoad() <= 0 && options_.GetGpuMemoryBytes() <= 0) {
    LOG_DEBUG("GPU module: both -g and -gm are zero, disabling");
    return false;
  }

  int next_id = 0;
  workers_.reserve(pending_devices_.size());
  for (auto &[idx, dev] : pending_devices_) {
    if (!dev->Init(idx)) {
      LOG_WARN("GPU device #%d failed to init, skipping", idx);
      continue;
    }
    LOG_INFO("GPU device #%d ready: %s", idx, dev->Name().c_str());

    // Optional memory load
    const int64_t mem_bytes = options_.GetGpuMemoryBytes();
    if (mem_bytes > 0) {
      if (!dev->AllocateMemory(static_cast<size_t>(mem_bytes))) {
        LOG_WARN("GPU device #%d memory allocation failed (%ld bytes); continuing", idx,
                 static_cast<long>(mem_bytes));
      }
    }

    auto ctx = std::make_unique<GpuWorkerContext>(next_id++, std::move(dev));
    if (options_.GetGpuLoad() > 0) {
      ctx->CalibrateBaseLoopCount();
      // Convert load (0..100) -> busy-ms within one tick.
      const int load_ms = options_.GetGpuLoad() * kScheduleIntervalMS / kGpuMaxLoadPerDevice;
      ctx->SetLoadSet(load_ms);
    }
    workers_.emplace_back(std::move(ctx));
  }
  pending_devices_.clear();

  if (workers_.empty()) {
    LOG_WARN("GPU module: no usable device, disabling");
    return false;
  }
  return true;
}

void GpuResourceManager::CreateWorkerThreads() {
  if (options_.GetGpuLoad() <= 0) {
    return;
  }
  for (auto &w : workers_) {
    w->Start();
  }
}

void GpuResourceManager::RequestWorkerThreadsStop() {
  for (auto &w : workers_) {
    w->RequestStop();
  }
}

void GpuResourceManager::JoinWorkerThreads() {
  for (auto &w : workers_) {
    w->Join();
  }
  // Workers hold the GpuDevice; releasing them here frees the contexts.
  workers_.clear();
}

void GpuResourceManager::Schedule(TimePoint /*time_point*/) {
  // GPU load is constant for now; future schedulers (mirroring CPU's
  // rand_normal) can rebalance per-worker load here.
}

}  // namespace gpu
