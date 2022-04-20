#pragma once

#include <memory>

#include "core/resource_manager.h"

namespace memory {

std::unique_ptr<core::ResourceManager> CreateResourceManager(const core::Options &options);

}
