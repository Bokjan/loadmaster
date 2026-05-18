#pragma once

#include <memory>

#include "core/resource_manager.h"

namespace gpu {

std::unique_ptr<core::ResourceManager> CreateResourceManager(const core::Options &options);

}  // namespace gpu
