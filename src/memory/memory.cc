#include "memory.h"

#include "manager_default.h"

namespace memory {

std::unique_ptr<ResourceManager> CreateResourceManager(const Options &options) {
  return std::move(std::unique_ptr<ResourceManager>(new MemoryResourceManagerDefault(options)));
}

}  // namespace memory
