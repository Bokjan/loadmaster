#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "cl_driver.h"
#include "gpu/device.h"

namespace gpu::opencl {

// OpenCL backed implementation of GpuDevice. Acts as the cross-vendor
// generic backend, primarily intended to cover hardware that the
// vendor-native paths miss -- most importantly Intel integrated GPUs
// (when Level Zero is unavailable) and AMD APU integrated GPUs (which
// ROCm/HIP refuses to support reliably). Also works with AMD dGPU and
// NVIDIA OpenCL ICDs as a universal last resort.
class OpenClDevice final : public GpuDevice {
 public:
  OpenClDevice();
  ~OpenClDevice() override;

  OpenClDevice(const OpenClDevice &) = delete;
  OpenClDevice &operator=(const OpenClDevice &) = delete;

  bool Init(int device_index) override;
  std::string Name() const override;
  bool AllocateMemory(std::size_t bytes) override;
  void ReleaseMemory() override;
  bool RunKernel(int loop_count) override;
  int ProbeBaseLoopCount(int target_ms) override;

  // Number of OpenCL GPU devices visible across all platforms. Returns 0
  // if the loader can't be opened (factory uses this to decide whether
  // to construct any OpenCl devices at all).
  static int DeviceCount();

 private:
  const ClApi *api_;
  cl_platform_id platform_;
  cl_device_id device_;
  cl_context context_;
  cl_command_queue queue_;
  cl_program program_;
  cl_kernel kernel_;
  cl_mem out_buf_;
  std::size_t out_bytes_;
  cl_mem mem_load_buf_;
  std::size_t mem_load_bytes_;
  std::string name_;
  int device_index_;

  void Destroy();
  // Walk every platform's GPU device list and return them flattened
  // (platform, device) in stable order. Used both by Init() and by the
  // static DeviceCount() probe.
  static std::vector<std::pair<cl_platform_id, cl_device_id>> CollectGpuDevices(const ClApi *api);
};

}  // namespace gpu::opencl
