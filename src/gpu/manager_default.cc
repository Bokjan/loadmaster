#include "manager_default.h"

namespace gpu {

GpuResourceManagerDefault::GpuResourceManagerDefault(
    const core::Options &options, std::vector<std::pair<int, std::unique_ptr<GpuDevice>>> devices)
    : GpuResourceManager(options, std::move(devices)) {}

}  // namespace gpu
