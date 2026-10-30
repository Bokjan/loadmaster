#pragma once

#include <map>
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
  // PCI address -> NVML handle, or nullptr if NVML doesn't know it.
  std::map<std::string, DeviceHandle> handles_;

  // Pids with a compute context on `device`, or false on error.
  bool ComputePids(DeviceHandle device, std::vector<int> &pids);
};

}  // namespace gpu::contention
