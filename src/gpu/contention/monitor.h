#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "source.h"

#include "core/resource_manager.h"

namespace gpu::contention {

// Hysteresis for one device: what to leave to other processes. Their
// share takes effect at once when it grows, and falls back only once a
// larger one hasn't been seen for kGpuYieldResumeMS.
class ShareGate final {
 public:
  // `others`: the percent of the device other processes took at this poll,
  // or nullopt when there were none. Returns the percent to leave them,
  // the most seen within kGpuYieldResumeMS, or nullopt once none has been
  // seen for that long.
  std::optional<double> Update(std::optional<double> others, TimePoint now);

 private:
  std::deque<std::pair<TimePoint, double>> seen_;
};

// Polls the sources every kGpuYieldPollMS on a thread of its own (a /proc
// scan can take tens of milliseconds, too long for the runtime's 100 ms
// main loop) and reports per-device changes in what to leave to other
// processes through `on_change`.
class Monitor final {
 public:
  // on_change(device index, its name, percent of the device to leave to
  // other processes or nullopt when there are none, one other pid or 0);
  // called from Poll().
  using OnChange = std::function<void(size_t, const std::string &, std::optional<int>, int)>;
  // Milliseconds of kernel time this process has run on device i so far:
  // taken out of the device's busy time to get the other processes' share.
  using OwnBusyMs = std::function<double(size_t)>;

  // `names` (one per device, for the log) and `devices` are parallel.
  Monitor(std::vector<std::string> names, std::vector<DeviceLocator> devices,
          std::vector<std::unique_ptr<Source>> sources, OnChange on_change,
          OwnBusyMs own_busy_ms = nullptr);
  ~Monitor();

  Monitor(const Monitor &) = delete;
  Monitor &operator=(const Monitor &) = delete;

  // Sample every source once and update the gates. Not thread-safe: the
  // first call comes from GpuResourceManager::Init, later ones from the
  // monitor thread.
  void Poll(TimePoint now);

  void Start();
  void RequestStop();
  void Join();

 private:
  std::vector<std::string> names_;
  std::vector<DeviceLocator> devices_;
  std::vector<std::unique_ptr<Source>> sources_;
  OnChange on_change_;
  OwnBusyMs own_busy_ms_;
  std::vector<ShareGate> gates_;
  // What on_change_ last reported per device.
  std::vector<std::optional<int>> reported_;
  // Own kernel time per device at the previous poll, and when that was.
  std::vector<double> own_busy_ms_last_;
  std::optional<TimePoint> last_poll_;
  // Polls in a row no source could see each device; warned once it
  // reaches kGpuYieldUnknownWarnPolls.
  std::vector<int> unknown_polls_;
  std::jthread thread_;

  void Loop(std::stop_token stoken);
};

}  // namespace gpu::contention
