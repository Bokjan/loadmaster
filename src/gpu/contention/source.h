#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "gpu/device.h"

namespace gpu::contention {

// What one source saw on one device during a poll.
struct Observation {
  enum class Verdict {
    kUnknown,  // this source can't see the device (no driver, no match, ...)
    kIdle,     // no other process doing compute work on it
    kBusy,     // some other process is
  };
  Verdict verdict = Verdict::kUnknown;
  int pid = 0;  // one of the other processes when kBusy, for the log; 0 if unknown
  // How busy the whole device was since the source's previous sample, in
  // percent of the time, counting every process (this one included).
  // nullopt where the source can't measure it; only needed when kBusy.
  std::optional<double> device_busy;
};

// One way of finding other processes on a GPU: a vendor library (NVML), a
// kernel interface (amdkfd, DRM fd accounting) or an OS counter (Windows
// GPU engine counters, macOS per-process GPU time). Each platform combines
// the sources it has; a device counts as busy when any of them says so.
//
// Only "compute" users count (CUDA / HIP / OpenCL / Level Zero contexts,
// compute engines), so a desktop compositor or browser doesn't make the
// GPU module stand down for good. Where the OS can't tell compute from
// graphics (macOS) the source says so in its own comment.
class Source {
 public:
  virtual ~Source() = default;

  // Short name for the log, e.g. "NVML".
  virtual const char *Name() const = 0;

  // Fill out[i] for devices[i]. `out` is pre-sized and kUnknown, except
  // for devices an earlier source already found kBusy and measured
  // (device_busy set): a source may skip those, as nothing it reports for
  // them changes the outcome. Called from the monitor thread only, except
  // for the first poll from GpuResourceManager::Init before that thread
  // starts.
  virtual void Sample(const std::vector<DeviceLocator> &devices, std::vector<Observation> &out) = 0;
};

// The sources available on this platform / host. Sources whose library or
// kernel interface is missing are left out, so the result may be empty.
std::vector<std::unique_ptr<Source>> CreatePlatformSources();

}  // namespace gpu::contention
