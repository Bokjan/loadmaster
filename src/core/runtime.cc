#include "runtime.h"

#include <thread>

#include "constants.h"
#include "options.h"

#include "core/signal_flag.h"
#include "cpu/factory.h"
#include "memory/factory.h"
#include "util/log.h"

namespace core {

const static FnCreateResourceManager resmgr_creators[] = {
    cpu::CreateResourceManager,
    memory::CreateResourceManager,
};

Runtime::PairMetaSignalHandler Runtime::meta_signal_handlers_[] = {
    {SIGINT, &Runtime::SigIntTerm},
    {SIGTERM, &Runtime::SigIntTerm},
    {SIGUSR1, &Runtime::SigUsr1},
    {SIGUSR2, &Runtime::SigUsr2},
};

Runtime::Runtime(const Options &options) : running_flag_(true), options_(options) {
  CreateSignalHandlerFunctors();
}

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
  [[likely]] while (running_flag_) {
    DealSignals();
    auto start = std::chrono::high_resolution_clock::now();
    for (auto &mgr : managers_) {
      mgr->Schedule(start);
    }
    std::this_thread::sleep_until(this->NextSchedulingTime(start));
  }
}

void Runtime::StopWorkers() {
  for (auto &mgr : managers_) {
    mgr->RequestWorkerThreadsStop();
  }
}

void Runtime::JoinWorkers() {
  for (auto &mgr : managers_) {
    mgr->JoinWorkerThreads();
  }
}

void Runtime::DealSignals() {
  auto &sf = core::SignalFlag::Get();
  for (const auto &h : signal_handlers_) {
    if (sf.Has(h.first)) {
      h.second();
      sf.Reset(h.first);
    }
  }
}

void Runtime::CreateSignalHandlerFunctors() {
  for (const auto &h : meta_signal_handlers_) {
    signal_handlers_.push_back(std::make_pair(h.first, std::bind(h.second, this)));
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
  constexpr int64_t kScaleFactor = 100'000'000;
  auto next_ns_count = next_us.count() / kScaleFactor * kScaleFactor;
  auto ns_diff = next_ns_count - start_us.count();
  return start_tp + std::chrono::nanoseconds(ns_diff);
}

void Runtime::SigIntTerm() { running_flag_ = false; }

void Runtime::SigUsr1() {}

void Runtime::SigUsr2() {}

}  // namespace core
