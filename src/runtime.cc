#include "runtime.h"

#include "constants.h"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "options.h"
#include "util/log.h"

#include <functional>

#include "unistd.h"

using FnCreateResourceManager =
    std::function<std::unique_ptr<ResourceManager>(const Options &options)>;

static const FnCreateResourceManager resmgr_creators[] = {cpu::CreateResourceManager,
                                                          memory::CreateResourceManager};

Runtime::Runtime(const Options &options) : options_(options), proc_stat_(getpid()) {}

void Runtime::Init() {
  // Create
  for (auto &&fn : resmgr_creators) {
    managers_.push_back(std::move(fn(options_)));
  }
  // Initialize
  for (auto it = managers_.begin(); it != managers_.end();) {
    if (!(*it)->Init()) {
      managers_.erase(it);
    } else {
      ++it;
    }
  }
}

void Runtime::CreateWorkers() {
  for (auto &mgr : managers_) {
    mgr->CreateWorkerThreads();
  }
}

void Runtime::MainLoop() {
  if (managers_.empty()) {
    LOG_FATAL("no module enabled, quit");
  }
  while (RunningFlag::Get().IsRunning()) {
    auto start = std::chrono::high_resolution_clock::now();
    proc_stat_.UpdateCpuStat(start);
    for (auto &mgr : managers_) {
      mgr->Schedule(start);
    }
    std::this_thread::sleep_until(start + std::chrono::milliseconds(kScheduleIntervalMS));
  }
}

void Runtime::JoinWorkers() {
  for (auto &mgr : managers_) {
    mgr->WaitThreads();
  }
}
