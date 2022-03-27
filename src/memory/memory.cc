#include "memory.h"

#include "manager_default.h"
#include "util/make_unique.h"

namespace memory {

std::unique_ptr<ResourceManager> CreateResourceManager(const Options &options) {
  return util::make_unique<MemoryResourceManagerDefault>(options);
}

}  // namespace memory
