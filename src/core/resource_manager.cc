#include "core/resource_manager.h"

namespace core {

WaitGroupDoneGuard::WaitGroupDoneGuard(util::WaitGroup &wg) : wg_(wg) {}

WaitGroupDoneGuard::~WaitGroupDoneGuard() { wg_.Done(); }

WorkerLoopGuard::WorkerLoopGuard(WorkerContext &ctx) : wgd_guard_(ctx.parent_wg_) {}

void ResourceManager::WaitThreads() const { wg_.Wait(); }

}  // namespace core