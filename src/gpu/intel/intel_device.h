#pragma once

#include <string>
#include <vector>

#include "gpu/device.h"
#include "ze_driver.h"

namespace gpu::intel {

// Level Zero (oneAPI) backed implementation of GpuDevice. Drives Intel
// integrated GPUs (Iris Xe / UHD / Arc Graphics on Tiger Lake and newer)
// and Intel Arc discrete GPUs through the vendor's ze_loader runtime.
//
// Linux/Windows only -- on macOS Apple does not ship a Level Zero
// driver, so this backend is excluded from the Apple build.
class IntelDevice final : public GpuDevice {
 public:
  IntelDevice();
  ~IntelDevice() override;

  IntelDevice(const IntelDevice &) = delete;
  IntelDevice &operator=(const IntelDevice &) = delete;

  bool Init(int device_index) override;
  std::string Name() const override;
  bool AllocateMemory(std::size_t bytes) override;
  void ReleaseMemory() override;
  bool RunKernel(int loop_count) override;

 private:
  const ZeApi *api_;
  ze_driver_handle_t driver_;
  ze_device_handle_t device_;
  ze_context_handle_t context_;
  ze_command_queue_handle_t queue_;
  ze_command_list_handle_t cmd_list_;
  ze_module_handle_t module_;
  ze_kernel_handle_t kernel_;
  void *out_ptr_;  // grid-sized scratch buffer
  std::size_t out_bytes_;
  // Optional `-gm` block, split into chunks of at most max_alloc_bytes_:
  // a single zeMemAllocDevice is capped at maxMemAllocSize (~4 GiB on
  // many parts) unless the relaxed-allocation-limits extension is used.
  std::vector<void *> mem_load_ptrs_;
  std::size_t mem_load_bytes_;
  std::size_t max_alloc_bytes_;
  std::string name_;
  int device_index_;

  void Destroy();
};

}  // namespace gpu::intel
