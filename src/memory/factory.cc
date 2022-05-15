#include "factory.h"

#include "manager_default.h"

namespace memory {

std::unique_ptr<core::ResourceManager> CreateResourceManager(const core::Options &options) {
  return std::make_unique<MemoryResourceManagerDefault>(options);
}

}  // namespace memory
