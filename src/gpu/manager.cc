#include "manager.h"

#include <chrono>
#include <optional>
#include <string>

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

    auto ctx = std::make_unique<GpuWorkerContext>(next_id, std::move(dev));
    // Optional memory load, allocated by the worker thread once started.
    if (options_.GetGpuMemoryBytes() > 0) {
      ctx->SetMemoryBytes(static_cast<size_t>(options_.GetGpuMemoryBytes()));
    }
    workers_.emplace_back(std::move(ctx));
    ++next_id;
  }
  pending_devices_.clear();

  if (workers_.empty()) {
    LOG_WARN("GPU module: no usable device, disabling");
    return false;
  }
  // Look for other users before calibrating: calibration runs the kernel
  // flat out, which would get in the way of a job already on the device
  // and come out too low there.
  InitContentionMonitor();
  if (options_.GetGpuLoad() > 0) {
    return CalibrateWorkers();
  }
  return true;
}

bool GpuResourceManager::CalibrateWorkers() {
  const size_t before = workers_.size();
  std::erase_if(workers_, [](const std::unique_ptr<GpuWorkerContext> &w) {
    if (w->Sharing()) {
      return false;  // calibrates on its own thread once there is room
    }
    if (w->CalibrateBaseLoopCount()) {
      return false;
    }
    // Can't run the kernel at all: drop the device instead of keeping a
    // worker that would fail on every tick.
    LOG_WARN("GPU %s failed kernel calibration, skipping", w->device()->Name().c_str());
    return true;
  });
  if (workers_.empty()) {
    LOG_WARN("GPU module: no usable device, disabling");
    return false;
  }
  if (workers_.size() != before) {
    // monitor_ addresses devices by their index in workers_.
    InitContentionMonitor();
  }
  for (auto &w : workers_) {
    // Initial load: subclasses are free to override on the first
    // Schedule() tick. We seed with the user-requested per-device load so
    // a worker that never sees Schedule() (e.g. the Default algorithm)
    // still runs at the intended rate.
    w->SetLoadSet(LoadPercentToBusyMs(options_.GetGpuLoad()));
  }
  return true;
}

void GpuResourceManager::InitContentionMonitor() {
  std::vector<std::string> names;
  std::vector<DeviceLocator> locators;
  for (const auto &w : workers_) {
    w->SetOthers(std::nullopt);  // the first Poll() below decides afresh
    names.push_back(w->device()->Name());
    locators.push_back(w->device()->Locator());
    LOG_DEBUG("GPU %s: pci=%s luid=0x%llx registry_id=0x%llx", names.back().c_str(),
              locators.back().pci_bus_id.empty() ? "?" : locators.back().pci_bus_id.c_str(),
              static_cast<unsigned long long>(locators.back().luid),
              static_cast<unsigned long long>(locators.back().registry_id));
  }
  auto on_change = [this](size_t i, const std::string &name, std::optional<int> others, int pid) {
    const bool was = workers_[i]->Sharing();
    if (!others) {
      LOG_INFO("GPU %s: no other process for %d s, back to the full load", name.c_str(),
               kGpuYieldResumeMS / kMillisecondsPerSecond);
    } else if (!was && pid > 0) {
      LOG_INFO("GPU %s: in use by another process (pid %d) taking %d%%, filling the rest",
               name.c_str(), pid, *others);
    } else if (!was) {
      LOG_INFO("GPU %s: in use by another process taking %d%%, filling the rest", name.c_str(),
               *others);
    } else {
      LOG_DEBUG("GPU %s: other processes take %d%%", name.c_str(), *others);
    }
    workers_[i]->SetOthers(others);
  };
  auto own_busy_ms = [this](size_t i) { return workers_[i]->BusyMs(); };
  monitor_ = std::make_unique<contention::Monitor>(std::move(names), std::move(locators),
                                                   contention::CreatePlatformSources(), on_change,
                                                   own_busy_ms);
  monitor_->Poll(std::chrono::steady_clock::now());
}

void GpuResourceManager::CreateWorkerThreads() {
  // Started even without `-g`: the worker also holds the `-gm` block and
  // gives it up while another process uses the device.
  for (auto &w : workers_) {
    w->Start();
  }
  if (monitor_ != nullptr) {
    monitor_->Start();
  }
}

void GpuResourceManager::RequestWorkerThreadsStop() {
  if (monitor_ != nullptr) {
    monitor_->RequestStop();
  }
  for (auto &w : workers_) {
    w->RequestStop();
  }
}

void GpuResourceManager::JoinWorkerThreads() {
  // The monitor calls into workers_: stop it before they go away.
  if (monitor_ != nullptr) {
    monitor_->Join();
    monitor_.reset();
  }
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
