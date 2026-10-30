#include "monitor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>

#include "gpu/constants.h"

#include "util/log.h"

namespace gpu::contention {

std::optional<double> ShareGate::Update(std::optional<double> others, TimePoint now) {
  while (!seen_.empty() &&
         now - seen_.front().first >= std::chrono::milliseconds(kGpuYieldResumeMS)) {
    seen_.pop_front();
  }
  if (others) {
    // Kept in decreasing order of share: a sighting is only of interest
    // until a larger, later one comes along.
    while (!seen_.empty() && seen_.back().second <= *others) {
      seen_.pop_back();
    }
    seen_.emplace_back(now, *others);
  }
  if (seen_.empty()) {
    return std::nullopt;
  }
  return seen_.front().second;
}

Monitor::Monitor(std::vector<std::string> names, std::vector<DeviceLocator> devices,
                 std::vector<std::unique_ptr<Source>> sources, OnChange on_change,
                 OwnBusyMs own_busy_ms)
    : names_(std::move(names)),
      devices_(std::move(devices)),
      sources_(std::move(sources)),
      on_change_(std::move(on_change)),
      own_busy_ms_(std::move(own_busy_ms)),
      gates_(devices_.size()),
      reported_(devices_.size()),
      own_busy_ms_last_(devices_.size(), 0.0),
      unknown_polls_(devices_.size(), 0) {}

Monitor::~Monitor() {
  // jthread requests stop and joins in its dtor.
}

void Monitor::Poll(TimePoint now) {
  using Verdict = Observation::Verdict;
  // Our own share of each device since the previous poll.
  std::vector<double> own(devices_.size(), 0.0);
  for (size_t i = 0; i < devices_.size() && own_busy_ms_; ++i) {
    const double busy = own_busy_ms_(i);
    if (last_poll_) {
      const double wall = std::chrono::duration<double, std::milli>(now - *last_poll_).count();
      if (wall > 0.0) {
        own[i] = std::clamp(100.0 * (busy - own_busy_ms_last_[i]) / wall, 0.0, 100.0);
      }
    }
    own_busy_ms_last_[i] = busy;
  }
  last_poll_ = now;

  std::vector<Observation> merged(devices_.size());
  std::vector<Observation> seen;
  for (auto &source : sources_) {
    // Devices an earlier source already found busy and measured go in as
    // such, so this one can skip them (DRM's /proc scan is the expensive
    // part).
    seen.assign(devices_.size(), Observation{});
    for (size_t i = 0; i < devices_.size(); ++i) {
      if (merged[i].verdict == Verdict::kBusy && merged[i].device_busy) {
        seen[i] = merged[i];
      }
    }
    source->Sample(devices_, seen);
    for (size_t i = 0; i < devices_.size(); ++i) {
      if (seen[i].verdict == Verdict::kBusy && merged[i].verdict != Verdict::kBusy) {
        merged[i].verdict = Verdict::kBusy;
        merged[i].pid = seen[i].pid;
        LOG_TRACE("GPU %s: %s sees pid %d", names_[i].c_str(), source->Name(), seen[i].pid);
      } else if (seen[i].verdict == Verdict::kIdle && merged[i].verdict == Verdict::kUnknown) {
        merged[i].verdict = Verdict::kIdle;
      }
      if (seen[i].device_busy) {
        merged[i].device_busy = std::max(merged[i].device_busy.value_or(0.0), *seen[i].device_busy);
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
      LOG_WARN("GPU %s: no way to see other processes using it, it will not make room for them",
               names_[i].c_str());
    }
    // The other processes' share: what the device was busy beyond our own
    // kernels, or all of it where nothing measures that.
    std::optional<double> others;
    if (merged[i].verdict == Verdict::kBusy) {
      others =
          merged[i].device_busy ? std::clamp(*merged[i].device_busy - own[i], 0.0, 100.0) : 100.0;
      LOG_TRACE("GPU %s: device %.1f%% busy, ours %.1f%%, others %.1f%%", names_[i].c_str(),
                merged[i].device_busy.value_or(-1.0), own[i], *others);
    }
    const auto level = gates_[i].Update(others, now);
    const std::optional<int> report =
        level ? std::optional<int>(static_cast<int>(std::lround(*level))) : std::nullopt;
    if (report != reported_[i]) {
      reported_[i] = report;
      if (on_change_) {
        on_change_(i, names_[i], report, merged[i].pid);
      }
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
