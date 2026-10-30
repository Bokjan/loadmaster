#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "gpu/device.h"
#include "gpu/mem_sweep.h"

namespace gpu::apple {

// Apple Metal backed implementation of GpuDevice. Drives the GPU via the
// Metal compute pipeline -- the only supported GPU compute API on macOS
// (NVIDIA CUDA and AMD ROCm/HIP have never been or are no longer
// available on Darwin).
//
// All Metal handles below are stored as type-erased `void *` so this
// header can be included by ordinary C++ translation units (factory.cc).
// The actual lifetimes are managed by metal_device.mm under ARC: each
// `void *` is a +1-retained `id<MTLxxx>` bridged out (CFBridgingRetain
// equivalent) at construction, and bridged back in (and released) in
// Destroy().
class AppleMetalDevice final : public GpuDevice {
 public:
  AppleMetalDevice();
  ~AppleMetalDevice() override;

  AppleMetalDevice(const AppleMetalDevice &) = delete;
  AppleMetalDevice &operator=(const AppleMetalDevice &) = delete;

  bool Init(int device_index) override;
  std::string Name() const override;
  DeviceLocator Locator() const override;
  bool AllocateMemory(size_t bytes) override;
  void ReleaseMemory() override;
  bool RunKernel(int loop_count, std::uint64_t mem_traffic_bytes) override;

  // Number of MTLDevices visible to the system. Implemented in the .mm.
  static int DeviceCount();

 private:
  // Type-erased handles to retained Objective-C objects:
  //   device_     : id<MTLDevice>
  //   queue_      : id<MTLCommandQueue>
  //   library_    : id<MTLLibrary>          (compiled MSL)
  //   pipeline_   : id<MTLComputePipelineState>
  //   out_buffer_ : id<MTLBuffer>           (grid-sized scratch)
  //   mem_buffer_ : id<MTLBuffer>           (optional `-gm` block)
  void *device_;
  void *queue_;
  void *library_;
  void *pipeline_;
  void *out_buffer_;
  void *mem_buffer_;
  std::size_t mem_bytes_;
  MemSweep sweep_;  // where the next launch's traffic goes in mem_buffer_
  std::string name_;
  int device_index_;

  void Destroy();
};

}  // namespace gpu::apple
