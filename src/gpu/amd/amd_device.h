#pragma once

#include <string>
#include <vector>

#include "hip_driver.h"
#include "gpu/device.h"

namespace gpu::amd {

class AmdDevice final : public GpuDevice {
 public:
  AmdDevice();
  ~AmdDevice() override;

  bool Init(int device_index) override;
  std::string Name() const override;
  bool AllocateMemory(size_t bytes) override;
  void ReleaseMemory() override;
  bool RunKernel(int loop_count) override;
  int ProbeBaseLoopCount(int target_ms) override;

 private:
  const HipApi *api_;
  hipDevice_t device_;
  hipCtx_t context_;
  hipModule_t module_;
  hipFunction_t busy_func_;
  void *out_ptr_;
  size_t out_bytes_;
  void *mem_load_ptr_;
  size_t mem_load_bytes_;
  std::string name_;
  int device_index_;

  bool MakeCurrent();
  void Destroy();
  // Compile kBusyKernelHipSrc with hipRTC into `code_out`. Returns false on
  // compile error (and logs the program log if available).
  bool CompileKernel(std::vector<char> &code_out);
};

}  // namespace gpu::amd
