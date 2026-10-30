#pragma once

#include <map>
#include <optional>
#include <string>

#include "source.h"

#include "util/dl.h"

namespace gpu::contention {

// NVIDIA, Linux and Windows: NVML's list of processes holding a compute
// (CUDA / NVIDIA OpenCL) context on a device -- what nvidia-smi shows as
// type "C". Graphics-only processes are a separate list and don't count.
// Covers every NVIDIA device whatever backend drives it (CUDA or OpenCL),
// matched by PCI address. Sees other users' processes without root.
//
// A device with other users is measured as a whole: the share of time
// any kernel ran on it (what nvidia-smi shows as GPU-Util), averaged over
// the driver's samples since the previous poll.
//
// Windows: only TCC / MCDM devices. Under WDDM every process with a
// D3D device ("C+G": explorer.exe, browsers, ...) is on the compute list
// too, so those devices are left to the GPU Engine counters (PdhSource).
class NvmlSource final : public Source {
 public:
  explicit NvmlSource(int self_pid);
  ~NvmlSource() override;

  NvmlSource(const NvmlSource &) = delete;
  NvmlSource &operator=(const NvmlSource &) = delete;

  // Whether NVML loaded and initialised.
  bool Available() const { return ready_; }

  const char *Name() const override { return "NVML"; }
  void Sample(const std::vector<DeviceLocator> &devices, std::vector<Observation> &out) override;

 private:
  using Return = int;
  using DeviceHandle = void *;

  int self_pid_;
  bool ready_;
  util::DlHandle lib_;
  Return (*init_)();
  Return (*shutdown_)();
  Return (*get_handle_by_pci_bus_id_)(const char *pci_bus_id, DeviceHandle *device);
  // nvmlDeviceGetComputeRunningProcesses_v3 / _v2 / v1; see the .cc for
  // the per-version entry size.
  Return (*get_compute_processes_)(DeviceHandle device, unsigned int *count, void *infos);
  size_t process_info_size_;
  // nvmlDeviceGetDriverModel(_v2), Windows only (nullptr elsewhere).
  Return (*get_driver_model_)(DeviceHandle device, int *current, int *pending);
  // Optional (nullptr if missing): nvmlDeviceGetSamples, and
  // nvmlDeviceGetUtilizationRates as the fallback. `samples` /
  // `utilization` point at nvmlSample_t / nvmlUtilization_t arrays.
  Return (*get_samples_)(DeviceHandle device, int type, unsigned long long last_seen_us,
                         int *value_type, unsigned int *count, void *samples);
  Return (*get_utilization_)(DeviceHandle device, void *utilization);
  // PCI address -> NVML handle, or nullptr if NVML doesn't know it.
  std::map<std::string, DeviceHandle> handles_;
  // Device -> timestamp of the newest utilization sample read from it.
  std::map<DeviceHandle, unsigned long long> last_sample_us_;

  // Pids with a compute context on `device`, or false on error.
  bool ComputePids(DeviceHandle device, std::vector<int> &pids);
  // Percent of the time since the previous call any kernel ran on
  // `device`, or nullopt if NVML can't tell.
  std::optional<double> DeviceBusy(DeviceHandle device);
};

}  // namespace gpu::contention
