#pragma once

#include <cstdint>
#include <map>
#include <utility>

#include "core/resource_manager.h"
#include "source.h"

namespace gpu::contention {

// macOS: the per-process GPU time Activity Monitor shows. Each process
// with a Metal device open has a user-client child under the GPU's
// IOAccelerator service ("pid 123, Name" in IOUserClientCreator), whose
// AppUsage entries carry accumulatedGPUTime in nanoseconds. A process
// counts once its GPU time grows by at least kGpuYieldMinBusyPercent of
// the wall time between polls.
//
// Metal doesn't separate compute from graphics, so this is busy-time
// based rather than "has a compute context", and WindowServer (the
// compositor, always drawing something) is left out.
class IoRegSource final : public Source {
 public:
  explicit IoRegSource(int self_pid);

  const char *Name() const override { return "IORegistry GPU time"; }
  void Sample(const std::vector<DeviceLocator> &devices, std::vector<Observation> &out) override;

 private:
  int self_pid_;
  // (registry_id, pid) -> accumulated GPU ns at the previous poll.
  std::map<std::pair<uint64_t, int>, uint64_t> last_ns_;
  TimePoint last_time_;
  bool primed_;
};

}  // namespace gpu::contention
