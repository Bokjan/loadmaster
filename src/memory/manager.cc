#include "manager.h"

#include "options.h"
#include "util/log.h"

namespace memory {

MemoryResourceManager::MemoryResourceManager(const Options &options) : ResourceManager(options) {}

bool MemoryResourceManager::Init() { return options_.GetMemoryMiB() > 0; }

void MemoryResourceManager::CreateWorkerThreads() {}

}  // namespace memory
