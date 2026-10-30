#pragma once

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "source.h"

#include "core/resource_manager.h"

namespace gpu::contention {

// Hysteresis for one device: yield at the first sighting of another
// process, resume only after kGpuYieldResumeMS without one.
class YieldGate final {
 public:
  // Returns whether the device should be left to the other processes.
  bool Update(bool others_present, TimePoint now);
  bool Yielding() const { return yielding_; }

 private:
  bool yielding_ = false;
  TimePoint last_seen_{};
};

// Polls the sources every kGpuYieldPollMS on a thread of its own (a /proc
// scan can take tens of milliseconds, too long for the runtime's 100 ms
// main loop) and reports per-device yield transitions through `on_change`.
class Monitor final {
 public:
  // on_change(device index, its name, yield, one other pid or 0); called
  // from Poll().
  using OnChange = std::function<void(size_t, const std::string &, bool, int)>;

  // `names` (one per device, for the log) and `devices` are parallel.
  Monitor(std::vector<std::string> names, std::vector<DeviceLocator> devices,
          std::vector<std::unique_ptr<Source>> sources, OnChange on_change);
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
  std::vector<YieldGate> gates_;
  // Polls in a row no source could see each device; warned once it
  // reaches kGpuYieldUnknownWarnPolls.
  std::vector<int> unknown_polls_;
  std::jthread thread_;

  void Loop(std::stop_token stoken);
};

}  // namespace gpu::contention
