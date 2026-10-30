#pragma once

#include <map>
#include <string>

#include "source.h"

namespace gpu::contention {

// Windows, every vendor: the "GPU Engine" performance counters Task
// Manager reads, one instance per (process, adapter, engine). A process
// counts when its compute engines ("Compute_0", "Cuda", ...; 3D / copy /
// video don't) keep the adapter at least kGpuYieldMinBusyPercent busy.
// Matched by adapter LUID; a device whose LUID the backend can't report
// (HIP) is compared against every adapter, which errs on yielding.
class PdhSource final : public Source {
 public:
  // A PDH_RAW_COUNTER, kept here so <pdh.h> stays out of the header.
  struct Raw {
    long status;
    long long first;
    long long second;
    unsigned long multi_count;
    unsigned long long time_stamp;
  };

  explicit PdhSource(unsigned long self_pid);
  ~PdhSource() override;

  PdhSource(const PdhSource &) = delete;
  PdhSource &operator=(const PdhSource &) = delete;

  // Whether the counter set exists here (Windows 10 1709+ with a WDDM 2.x
  // driver).
  bool Available() const { return available_; }

  const char *Name() const override { return "GPU Engine counters"; }
  void Sample(const std::vector<DeviceLocator> &devices, std::vector<Observation> &out) override;

 private:
  unsigned long self_pid_;
  bool available_;
  // Raw value of each instance at the previous poll.
  std::map<std::wstring, Raw> last_;

  // Open a fresh query, collect once and return every instance's raw value
  // (and, with `prev`, the utilization since then). False on failure.
  bool Collect(std::map<std::wstring, Raw> &raw, const std::map<std::wstring, Raw> *prev,
               std::map<std::wstring, double> *percent);
};

}  // namespace gpu::contention
