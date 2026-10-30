#pragma once

#include <memory>
#include <vector>

#include "contention/monitor.h"
#include "worker.h"

#include "core/resource_manager.h"

namespace gpu {

// Abstract base for GPU resource managers. Owns the per-device worker
// contexts and provides the lifecycle plumbing; concrete subclasses
// (Default, RandomNormal) decide HOW the per-tick load is set.
//
// Init() takes the list of (vendor-local index, device) pairs the
// factory probed, binds each device, and constructs one GpuWorkerContext
// per usable device (which allocates the optional device memory once
// started). It then sets up the contention monitor that makes a device
// yield -- no kernels, no `-gm` block -- while another process is doing
// compute work on it, and only then calibrates the devices nobody else is
// using.
// Subclasses can read `workers_` from their `Schedule()` override to
// drive per-device loads.
class GpuResourceManager : public core::ResourceManager {
 public:
  // Takes ownership of a (possibly empty) list of devices. Init() will
  // bind each device to its index, calibrate, and optionally allocate
  // device memory according to options.
  GpuResourceManager(const core::Options &options,
                     std::vector<std::pair<int, std::unique_ptr<GpuDevice>>> devices);

  bool Init() override;
  void CreateWorkerThreads() override final;
  void RequestWorkerThreadsStop() override final;
  void JoinWorkerThreads() override final;

 protected:
  // Pairs of (vendor-local device index, device impl) supplied by the
  // factory; consumed by Init().
  std::vector<std::pair<int, std::unique_ptr<GpuDevice>>> pending_devices_;
  // GpuWorkerContext holds non-movable members (std::atomic, std::jthread),
  // so we own them via unique_ptr to make the vector copyable/movable.
  std::vector<std::unique_ptr<GpuWorkerContext>> workers_;
  std::unique_ptr<contention::Monitor> monitor_;

  // Convert a load percentage (0..kGpuMaxLoadPerDevice) into the
  // per-tick busy-millisecond budget used by GpuWorkerContext.
  static int LoadPercentToBusyMs(int load_percent);

 private:
  // Build monitor_ over workers_ and take the first sample, so a device
  // that is already in use starts out yielding.
  void InitContentionMonitor();
  // Calibrate every worker that isn't yielding and drop the ones that
  // can't run the kernel; yielding ones calibrate once they resume. False
  // if no device is left.
  bool CalibrateWorkers();
};

}  // namespace gpu
