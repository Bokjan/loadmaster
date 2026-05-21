#pragma once

#include "manager.h"

namespace gpu {

// Default scheduler: each device runs at the user-requested per-device
// load (`-g <load>`) for the entire run. There is nothing to do on each
// Schedule() tick -- the worker thread observes the load via atomic and
// keeps doing the same amount of work every period.
class GpuResourceManagerDefault final : public GpuResourceManager {
 public:
  GpuResourceManagerDefault(const core::Options &options,
                            std::vector<std::pair<int, std::unique_ptr<GpuDevice>>> devices);

  // Inherit the no-op `Schedule()` from `core::ResourceManager`.
};

}  // namespace gpu
