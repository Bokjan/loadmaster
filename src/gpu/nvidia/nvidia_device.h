#pragma once

#include <string>

#include "cuda_driver.h"
#include "gpu/device.h"

namespace gpu::nvidia {

class NvidiaDevice final : public GpuDevice {
 public:
  NvidiaDevice();
  ~NvidiaDevice() override;

  bool Init(int device_index) override;
  std::string Name() const override;
  bool AllocateMemory(size_t bytes) override;
  void ReleaseMemory() override;
  bool RunKernel(int loop_count) override;
  int ProbeBaseLoopCount(int target_ms) override;

 private:
  const CudaApi *api_;
  CUdevice device_;
  CUcontext context_;
  CUmodule module_;
  CUfunction busy_func_;
  CUdeviceptr out_ptr_;            // scratch buffer written by the kernel
  size_t out_bytes_;
  CUdeviceptr mem_load_ptr_;       // optional memory-load block
  size_t mem_load_bytes_;
  std::string name_;
  int device_index_;

  bool MakeCurrent();
  void Destroy();
};

}  // namespace gpu::nvidia
