#pragma once

#include <string>
#include <vector>

#include "gpu/device.h"
#include "gpu/mem_sweep.h"
#include "hip_driver.h"

namespace gpu::amd {

class AmdDevice final : public GpuDevice {
 public:
  AmdDevice();
  ~AmdDevice() override;

  bool Init(int device_index) override;
  std::string Name() const override;
  DeviceLocator Locator() const override;
  std::optional<double> MemoryBandwidth() const override;
  bool AllocateMemory(size_t bytes) override;
  void ReleaseMemory() override;
  bool RunKernel(int loop_count, std::uint64_t mem_traffic_bytes) override;

 private:
  const HipApi *api_;
  hipDevice_t device_;
  hipCtx_t context_;
  hipModule_t module_;
  hipFunction_t busy_func_;
  void *out_ptr_;
  size_t out_bytes_;
  // Init() got work through to the device, so our amdkfd queue exists.
  bool queue_created_;
  void *mem_load_ptr_;
  size_t mem_load_bytes_;
  MemSweep sweep_;  // where the next launch's traffic goes in that block
  std::string name_;
  int device_index_;

  bool MakeCurrent();
  void Destroy();
  // Compile kBusyKernelHipSrc with hipRTC into `code_out`. Returns false on
  // compile error (and logs the program log if available).
  bool CompileKernel(std::vector<char> &code_out);
};

}  // namespace gpu::amd
