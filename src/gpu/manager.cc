#include "manager.h"

#include "constants.h"

#include "core/constants.h"
#include "core/options.h"
#include "util/log.h"

namespace gpu {

GpuResourceManager::GpuResourceManager(
    const core::Options &options, std::vector<std::pair<int, std::unique_ptr<GpuDevice>>> devices)
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
      // Initial load: subclasses are free to override on the first
      // Schedule() tick. We seed with the user-requested per-device
      // load so a worker that never sees Schedule() (e.g. the Default
      // algorithm) still runs at the intended rate.
      ctx->SetLoadSet(LoadPercentToBusyMs(options_.GetGpuLoad()));
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

int GpuResourceManager::LoadPercentToBusyMs(int load_percent) {
  // Mirror the CPU mapping: load% of one full scheduling period.
  return load_percent * kScheduleIntervalMS / kGpuMaxLoadPerDevice;
}

}  // namespace gpu
