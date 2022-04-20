#include "memory/memory.h"

#include "memory/manager_default.h"

namespace memory {

std::unique_ptr<ResourceManager> CreateResourceManager(const Options &options) {
  return std::make_unique<MemoryResourceManagerDefault>(options);
}

}  // namespace memory
