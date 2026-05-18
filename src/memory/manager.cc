#include "manager.h"

#include "core/options.h"
#include "util/log.h"

namespace memory {

MemoryResourceManager::MemoryResourceManager(const core::Options &options)
    : ResourceManager(options) {}

bool MemoryResourceManager::Init() { return options_.GetMemoryBytes() > 0; }

void MemoryResourceManager::CreateWorkerThreads() {}

void MemoryResourceManager::RequestWorkerThreadsStop() {}

void MemoryResourceManager::JoinWorkerThreads() {}

}  // namespace memory
