#include "runtime.h"

#include <functional>

#include "constants.h"
#include "core/options.h"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "util/log.h"

namespace core {

using FnCreateResourceManager =
    std::function<std::unique_ptr<ResourceManager>(const Options &options)>;

const static FnCreateResourceManager resmgr_creators[] = {cpu::CreateResourceManager,
                                                          memory::CreateResourceManager};

Runtime::Runtime(const Options &options) : options_(options) {}

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
    LOG_FATAL("no module is enabled, quit");
  }
  while (RunningFlag::Get().IsRunning()) [[likely]] {
    auto start = std::chrono::high_resolution_clock::now();
    for (auto &mgr : managers_) {
      mgr->Schedule(start);
    }
    std::this_thread::sleep_until(this->NextSchedulingTime(start));
  }
}

void Runtime::JoinWorkers() {
  for (auto &mgr : managers_) {
    mgr->WaitThreads();
  }
}

void Runtime::StopWorkers() {
  for (auto &mgr : managers_) {
    mgr->RequestWorkerThreadsStop();
  }
}

TimePoint Runtime::NextSchedulingTime(TimePoint start_tp) {
  // Calc start + 100ms
  auto start_us = std::chrono::duration_cast<std::chrono::nanoseconds>(start_tp.time_since_epoch());
  auto next_us = start_us + std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::milliseconds(kScheduleIntervalMS));
  // Floor to 100 millisecond
  // ns -> 1e-9 s
  // ms -> 1e-3 s
  // 100ms -> 1e-1 s
  constexpr int64_t kScaleFactor = 100000000;
  auto next_ns_count = next_us.count() / kScaleFactor * kScaleFactor;
  auto ns_diff = next_ns_count - start_us.count();
  return start_tp + std::chrono::nanoseconds(ns_diff);
}

}  // namespace core
