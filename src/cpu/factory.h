#pragma once

#include <memory>

#include "core/resource_manager.h"

namespace cpu {

std::unique_ptr<core::ResourceManager> CreateResourceManager(const core::Options &options);

}  // namespace cpu
