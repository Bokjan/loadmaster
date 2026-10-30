#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace gpu {

// Where to find a device in the OS / driver process accounting, so the
// contention monitor can tell whether some other process is using it.
// Every field is optional; a source that needs one the backend could not
// provide reports "unknown" for that device.
struct DeviceLocator {
  // PCI address, canonical "dddd:bb:dd.f" (lower-case hex); see
  // contention::CanonicalPciBusId. Empty if the backend can't tell.
  std::string pci_bus_id;
  // Windows adapter LUID as (HighPart << 32) | LowPart; 0 if unknown.
  std::uint64_t luid = 0;
  // macOS: IORegistry entry ID of the device's IOAccelerator; 0 if unknown.
  std::uint64_t registry_id = 0;
  // Linux: this process holds an amdkfd (ROCm) queue on the device, so it
  // shows up in /sys/class/kfd itself. Lets the KFD source discount its own
  // entry when it can't recognise our pid (PID namespaces). Backends set it
  // only once Init() got work through to the device: ROCm creates the queue
  // lazily, and discounting an entry we don't have would hide a real user.
  bool kfd_client = false;
};

// Vendor-agnostic interface to a single GPU device. Concrete impls live in
// gpu/{nvidia,amd,intel,opencl,apple}/ and are constructed by gpu::factory.
//
// Lifetime:
//
//   ctor -> Init() -> Locator() -> RunKernel() ... RunKernel()
//        -> [AllocateMemory() / ReleaseMemory() ...] -> dtor (releases context)
//
// Threading: calls are never concurrent, but they are NOT all made from
// one thread. Init(), Locator() and the calibration RunKernel() calls
// happen on the main thread (GpuResourceManager::Init); the steady-state
// RunKernel() and the AllocateMemory() / ReleaseMemory() calls happen on
// the device's worker thread;
// destruction happens on the main thread again after the worker joined.
// Backends with per-thread "current context" state (CUDA, HIP) must
// therefore make their context current at the top of every call.
//
// Init() returning false means "this device is unusable"; the caller should
// drop it and continue with the remaining devices.
class GpuDevice {
 public:
  virtual ~GpuDevice() = default;

  // Bind to the underlying device (creates a context, loads the kernel).
  // `device_index` is vendor-local.
  virtual bool Init(int device_index) = 0;

  // Human-readable device name, e.g. "NVIDIA GeForce RTX 4090 (#0)".
  virtual std::string Name() const = 0;

  // Identity for the contention monitor. Only valid after Init().
  virtual DeviceLocator Locator() const { return {}; }

  // Peak memory bandwidth in bytes per second as the driver reports it
  // (memory clock x bus width, double data rate), or nullopt where the
  // backend can't tell. Sets the `-gm` block's traffic rate. Only valid
  // after Init().
  virtual std::optional<double> MemoryBandwidth() const { return std::nullopt; }

  // Allocate `bytes` of device memory and overwrite it once to force
  // physical commit. Safe to call multiple times (previous block freed).
  virtual bool AllocateMemory(size_t bytes) = 0;
  virtual void ReleaseMemory() = 0;

  // Launch the busy kernel with the given per-thread loop count and block
  // until completion. Returns false on launch / sync failure. Kernel time
  // is linear in `loop_count`; calibration lives in GpuWorkerContext.
  // Along the way the kernel reads and writes back about
  // `mem_traffic_bytes` of the block from AllocateMemory(), continuing
  // where the previous launch stopped (see gpu::MemSweep); no traffic
  // without a block.
  virtual bool RunKernel(int loop_count, std::uint64_t mem_traffic_bytes) = 0;
};

}  // namespace gpu
