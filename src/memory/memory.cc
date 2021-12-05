#include "memory.h"

#include "master.h"

namespace memory {

std::unique_ptr<ResourceManager> CreateResourceManager(const Options &options) {
  return std::move(std::unique_ptr<ResourceManager>(new MemoryResourceManagerSimple(options)));
}

}  // namespace memory
