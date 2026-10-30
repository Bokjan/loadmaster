#pragma once

#include <map>
#include <set>
#include <string>

#include "source.h"

namespace gpu::contention {

// AMD ROCm (HIP, ROCm OpenCL) on Linux. Every amdkfd client has a
// directory /sys/class/kfd/kfd/proc/<pid>, and each user-mode queue it
// created a queues/<id>/gpuid file naming the GPU the queue runs on.
// Holding a queue on a device is what "doing compute work on it" means
// here; graphics never goes through amdkfd. World-readable, so other
// users' processes are visible too.
class KfdSource final : public Source {
 public:
  // `root` prefixes /sys (tests point it at a fake tree); `self_pid` is the
  // pid to ignore.
  KfdSource(std::string root, int self_pid);

  // Whether the amdkfd topology exists under `root`.
  static bool Available(const std::string &root);

  const char *Name() const override { return "amdkfd"; }
  void Sample(const std::vector<DeviceLocator> &devices, std::vector<Observation> &out) override;

 private:
  std::string root_;
  int self_pid_;
  // PCI address with the function forced to 0 -> amdkfd gpu_ids, from the
  // topology (fixed for the run). A partitioned GPU (MI300 CPX, ...) has a
  // node per partition, each ORing its node id into location_id's low
  // (function) bits.
  std::map<std::string, std::set<uint32_t>> gpu_ids_;
};

}  // namespace gpu::contention
