#pragma once

#include <memory>
#include <vector>

#include "worker.h"

#include "core/resource_manager.h"

namespace gpu {

class GpuResourceManager final : public core::ResourceManager {
 public:
  // Takes ownership of a (possibly empty) list of devices. Init() will
  // bind each device to its index, calibrate, and optionally allocate
  // device memory according to options.
  GpuResourceManager(const core::Options &options,
                     std::vector<std::pair<int, std::unique_ptr<GpuDevice>>> devices);

  bool Init() override;
  void CreateWorkerThreads() override;
  void RequestWorkerThreadsStop() override;
  void JoinWorkerThreads() override;
  void Schedule(TimePoint time_point) override;

 private:
  // Pairs of (vendor-local device index, device impl) supplied by the
  // factory. We keep the index so logs / errors stay informative.
  std::vector<std::pair<int, std::unique_ptr<GpuDevice>>> pending_devices_;
  // GpuWorkerContext holds non-movable members (std::atomic, std::jthread),
  // so we own them via unique_ptr to make the vector copyable/movable.
  std::vector<std::unique_ptr<GpuWorkerContext>> workers_;
};

}  // namespace gpu
