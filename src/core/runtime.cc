#include "runtime.h"

#include <thread>
#include <utility>

#include "constants.h"
#include "options.h"

#include "core/signal_flag.h"
#include "util/log.h"

namespace core {

Runtime::PairMetaSignalHandler Runtime::meta_signal_handlers_[] = {
    {SIGINT, &Runtime::SigIntTerm},
    {SIGTERM, &Runtime::SigIntTerm},
#if !IS_WINDOWS
    {SIGUSR1, &Runtime::SigUsr1},
    {SIGUSR2, &Runtime::SigUsr2},
#endif
};

Runtime::Runtime(const Options &options, std::vector<FnCreateResourceManager> creators)
    : running_flag_(true), options_(options), creators_(std::move(creators)) {
  CreateSignalHandlerFunctors();
}

void Runtime::Run() {
  Init();
  CreateWorkers();
  MainLoop();
  StopWorkers();
  JoinWorkers();
}

void Runtime::Init() {
  // Create
  for (auto &fn : creators_) {
    if (!fn) {
      continue;
    }
    auto mgr = fn(options_);
    if (mgr) {
      managers_.emplace_back(std::move(mgr));
    }
  }
  // Initialize; drop those that fail to init.
  for (auto it = managers_.begin(); it != managers_.end();) {
    if (!(*it)->Init()) {
      it = managers_.erase(it);
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
    LOG_WARN("no module is enabled, quit");
    return;
  }
  [[likely]] while (running_flag_) {
    DealSignals();
    const auto start = std::chrono::high_resolution_clock::now();
    for (auto &mgr : managers_) {
      mgr->Schedule(start);
    }
    std::this_thread::sleep_until(NextSchedulingTime(start));
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
    signal_handlers_.emplace_back(h.first, std::bind(h.second, this));
  }
}

TimePoint Runtime::NextSchedulingTime(TimePoint start_tp) {
  // Align "start + kScheduleIntervalMS" down to the nearest
  // kScheduleIntervalMS boundary so all scheduling ticks land on a stable
  // wall-clock grid.
  using namespace std::chrono;
  const auto start_ns = duration_cast<nanoseconds>(start_tp.time_since_epoch());
  const auto next_ns = start_ns + duration_cast<nanoseconds>(milliseconds(kScheduleIntervalMS));
  constexpr int64_t kScaleNs = static_cast<int64_t>(kScheduleIntervalMS) * 1'000'000;
  const auto floored_ns = next_ns.count() / kScaleNs * kScaleNs;
  const auto delta = floored_ns - start_ns.count();
  return start_tp + nanoseconds(delta);
}

void Runtime::SigIntTerm() { running_flag_ = false; }

void Runtime::SigUsr1() {}

void Runtime::SigUsr2() {}

}  // namespace core
