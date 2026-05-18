#pragma once

#include <cstddef>
#include <string>

namespace gpu {

// Vendor-agnostic interface to a single GPU device. Concrete impls live in
// gpu/nvidia/ and gpu/amd/ and are constructed by gpu::factory.
//
// All methods are expected to be called from the same thread (the per-device
// worker thread). Lifetime:
//
//   ctor -> Init() -> [AllocateMemory()] -> [ProbeBaseLoopCount()]
//                  -> RunKernel() ... RunKernel()
//                  -> [ReleaseMemory()] -> dtor (releases context)
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
  // until completion. Returns false on launch / sync failure.
  virtual bool RunKernel(int loop_count) = 0;

  // Find a `loop_count` such that one RunKernel() call takes roughly
  // `target_ms` milliseconds. Called once at startup.
  virtual int ProbeBaseLoopCount(int target_ms) = 0;
};

}  // namespace gpu
