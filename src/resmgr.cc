#include "resmgr.h"

WorkerLoopGuard::WorkerLoopGuard(WorkerContext &ctx) : ctx_(ctx) {}

WorkerLoopGuard::~WorkerLoopGuard() { ctx_.OnFinish(); }

void ResourceManager::WaitThreads() const { wg_.Wait(); }
