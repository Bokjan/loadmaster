#include "runtime.h"

#include "constants.h"
#include "cpu/master.h"
#include "global.h"
#include "options.h"
#include "util/log.h"

Runtime::Runtime(const Options &options) : options_(options) {}

void Runtime::Init() {
  // CPU
  managers_.push_back(
      std::unique_ptr<ResourceManager>(new cpu::CpuResourceManagerSimple(options_)));
  // Initialize
  for (auto &mgr : managers_) {
    mgr->InitWithOptions(options_);
  }
}

void Runtime::CreateWorkers() {
  for (auto &mgr : managers_) {
    mgr->CreateWorkerThreads();
  }
}

void Runtime::MainLoop() {
  while (global::keep_loop) {
    auto start = std::chrono::high_resolution_clock::now();
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
