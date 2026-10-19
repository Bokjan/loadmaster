#pragma once

#include <cstddef>
#include <string>

namespace gpu {

// Vendor-agnostic interface to a single GPU device. Concrete impls live in
// gpu/{nvidia,amd,intel,opencl,apple}/ and are constructed by gpu::factory.
//
// Lifetime:
//
//   ctor -> Init() -> [AllocateMemory()] -> RunKernel() ... RunKernel()
//        -> [ReleaseMemory()] -> dtor (releases context)
//
// Threading: calls are never concurrent, but they are NOT all made from
// one thread. Init(), AllocateMemory() and the calibration RunKernel()
// calls happen on the main thread (GpuResourceManager::Init); the
// steady-state RunKernel() calls happen on the device's worker thread;
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

  // Allocate `bytes` of device memory and overwrite it once to force
  // physical commit. Safe to call multiple times (previous block freed).
  virtual bool AllocateMemory(size_t bytes) = 0;
  virtual void ReleaseMemory() = 0;

  // Launch the busy kernel with the given per-thread loop count and block
  // until completion. Returns false on launch / sync failure. Kernel time
  // is linear in `loop_count`; calibration lives in GpuWorkerContext.
  virtual bool RunKernel(int loop_count) = 0;
};

}  // namespace gpu
