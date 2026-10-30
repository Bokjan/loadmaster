#include "monitor.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

#include "gpu/constants.h"

#include "util/log.h"

namespace gpu::contention {

bool YieldGate::Update(bool others_present, TimePoint now) {
  if (others_present) {
    yielding_ = true;
    last_seen_ = now;
  } else if (yielding_ && now - last_seen_ >= std::chrono::milliseconds(kGpuYieldResumeMS)) {
    yielding_ = false;
  }
  return yielding_;
}

Monitor::Monitor(std::vector<std::string> names, std::vector<DeviceLocator> devices,
                 std::vector<std::unique_ptr<Source>> sources, OnChange on_change)
    : names_(std::move(names)),
      devices_(std::move(devices)),
      sources_(std::move(sources)),
      on_change_(std::move(on_change)),
      gates_(devices_.size()),
      unknown_polls_(devices_.size(), 0) {}

Monitor::~Monitor() {
  // jthread requests stop and joins in its dtor.
}

void Monitor::Poll(TimePoint now) {
  using Verdict = Observation::Verdict;
  std::vector<Observation> merged(devices_.size());
  std::vector<Observation> seen;
  for (auto &source : sources_) {
    // Devices an earlier source already found busy go in as such, so this
    // one can skip them (DRM's /proc scan is the expensive part).
    seen.assign(devices_.size(), Observation{});
    for (size_t i = 0; i < devices_.size(); ++i) {
      if (merged[i].verdict == Verdict::kBusy) {
        seen[i] = merged[i];
      }
    }
    source->Sample(devices_, seen);
    for (size_t i = 0; i < devices_.size(); ++i) {
      if (seen[i].verdict == Verdict::kBusy && merged[i].verdict != Verdict::kBusy) {
        merged[i] = seen[i];
        LOG_TRACE("GPU %s: %s sees pid %d", names_[i].c_str(), source->Name(), seen[i].pid);
      } else if (seen[i].verdict == Verdict::kIdle && merged[i].verdict == Verdict::kUnknown) {
        merged[i].verdict = Verdict::kIdle;
      }
    }
  }
  for (size_t i = 0; i < devices_.size(); ++i) {
    if (merged[i].verdict != Verdict::kUnknown) {
      // Once warned, stay warned: no repeat if the device flaps.
      if (unknown_polls_[i] < kGpuYieldUnknownWarnPolls) {
        unknown_polls_[i] = 0;
      }
    } else if (++unknown_polls_[i] == kGpuYieldUnknownWarnPolls) {
      LOG_WARN("GPU %s: no way to see other processes using it, it will not yield to them",
               names_[i].c_str());
    }
    const bool was = gates_[i].Yielding();
    const bool now_yielding = gates_[i].Update(merged[i].verdict == Verdict::kBusy, now);
    if (now_yielding != was && on_change_) {
      on_change_(i, names_[i], now_yielding, merged[i].pid);
    }
  }
}

void Monitor::Start() {
  if (sources_.empty()) {
    return;  // nothing could ever report a device busy (BSD)
  }
  thread_ = std::jthread([this](std::stop_token stoken) { Loop(stoken); });
}

void Monitor::RequestStop() { thread_.request_stop(); }

void Monitor::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void Monitor::Loop(std::stop_token stoken) {
  // Wait on a condition variable rather than sleeping: a stop request
  // wakes the thread at once instead of after a whole poll period.
  std::mutex mutex;
  std::condition_variable_any wake;
  std::unique_lock lock(mutex);
  const auto period = std::chrono::milliseconds(kGpuYieldPollMS);
  while (!wake.wait_for(lock, stoken, period, [&stoken] { return stoken.stop_requested(); })) {
    Poll(std::chrono::steady_clock::now());
  }
}

}  // namespace gpu::contention
