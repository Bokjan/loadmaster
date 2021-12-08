#include "manager.h"

#include "options.h"
#include "util/log.h"

namespace memory {

MemoryResourceManager::MemoryResourceManager(const Options &options)
    : ResourceManager(options), block_ptr_(nullptr) {}

bool MemoryResourceManager::Init() { return options_.memory_ > 0; }

void MemoryResourceManager::CreateWorkerThreads() {}

}  // namespace memory
