#pragma once

#include <chrono>
#include <map>
#include <string>
#include <utility>

#include "parse.h"
#include "source.h"

namespace gpu::contention {

// Linux DRM drivers (amdgpu, i915, xe, nouveau, ...): covers Intel Level
// Zero / OpenCL and Mesa OpenCL, which don't go through amdkfd. A process
// counts when it holds one of the device's /dev/dri nodes open, has a
// compute runtime for that driver mapped (parse.h MapsComputeRuntime) --
// the node alone would also catch every compositor and browser -- and its
// DRM client keeps the device's engines at least kGpuYieldMinBusyPercent
// busy (fdinfo usage stats). The last check weeds out programs that merely
// enumerated OpenCL platforms, which maps every installed ICD; drivers
// without usage stats fall back to the first two. The device's busy time
// is the sum over every client's (ours included); where some client has
// no usage stats, amdgpu's gpu_busy_percent stands in if there is one.
//
// Reading another process's fd table, fdinfo and maps needs ptrace-read
// access to it, i.e. the same user or root: run as root to see everybody.
class DrmSource final : public Source {
 public:
  // `root` prefixes /proc and /sys (tests point it at a fake tree);
  // `self_pid` is our own pid, which only adds to the busy time.
  DrmSource(std::string root, int self_pid);

  const char *Name() const override { return "DRM"; }
  void Sample(const std::vector<DeviceLocator> &devices, std::vector<Observation> &out) override;

 private:
  using Clock = std::chrono::steady_clock;
  struct Node {
    std::string pci;
    std::string driver;  // "i915", "amdgpu", ...
  };
  struct Reading {
    DrmFdinfo info;
    Clock::time_point at;
  };

  std::string root_;
  int self_pid_;
  // "/dev/dri/renderD128" -> its device, for every DRM node of a PCI
  // device (fixed for the run). The proprietary NVIDIA driver is left out:
  // CUDA never opens its DRM nodes, NVML covers it.
  std::map<std::string, Node> nodes_;
  // (pid, DRM client id) -> its usage stats at the previous poll, the
  // baseline for the next one.
  std::map<std::pair<int, uint64_t>, Reading> last_;
};

}  // namespace gpu::contention
