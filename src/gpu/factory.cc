#include "factory.h"

#include <utility>
#include <vector>

#include "amd/amd_device.h"
#include "amd/hip_driver.h"
#include "manager.h"
#include "nvidia/cuda_driver.h"
#include "nvidia/nvidia_device.h"

#include "core/options.h"
#include "util/log.h"

namespace gpu {

namespace {

using IndexedDevice = std::pair<int, std::unique_ptr<GpuDevice>>;

// Build the list of (index, device) pairs for a given vendor by enumerating
// available devices and filtering against the user's `-gi` selection.
std::vector<IndexedDevice> EnumerateNvidia(const core::Options &options) {
  std::vector<IndexedDevice> out;
  const auto *api = nvidia::LoadCudaApi();
  if (api == nullptr) {
    return out;
  }
  int count = 0;
  if (api->cuDeviceGetCount(&count) != nvidia::CUDA_SUCCESS || count <= 0) {
    LOG_INFO("NVIDIA: no devices reported by cuDeviceGetCount");
    return out;
  }
  LOG_INFO("NVIDIA: %d device(s) detected", count);

  auto wants = [&](int idx) -> bool {
    if (options.GpuUseAllDevices()) {
      return true;
    }
    for (int wanted : options.GetGpuIndices()) {
      if (wanted == idx) {
        return true;
      }
    }
    return false;
  };
  for (int i = 0; i < count; ++i) {
    if (!wants(i)) {
      continue;
    }
    out.emplace_back(i, std::make_unique<nvidia::NvidiaDevice>());
  }
  return out;
}

std::vector<IndexedDevice> EnumerateAmd(const core::Options &options) {
  std::vector<IndexedDevice> out;
  const auto *api = amd::LoadHipApi();
  if (api == nullptr) {
    return out;
  }
  int count = 0;
  if (api->hipGetDeviceCount(&count) != amd::hipSuccess || count <= 0) {
    LOG_INFO("AMD: no devices reported by hipGetDeviceCount");
    return out;
  }
  LOG_INFO("AMD: %d device(s) detected", count);

  auto wants = [&](int idx) -> bool {
    if (options.GpuUseAllDevices()) {
      return true;
    }
    for (int wanted : options.GetGpuIndices()) {
      if (wanted == idx) {
        return true;
      }
    }
    return false;
  };
  for (int i = 0; i < count; ++i) {
    if (!wants(i)) {
      continue;
    }
    out.emplace_back(i, std::make_unique<amd::AmdDevice>());
  }
  return out;
}

}  // namespace

std::unique_ptr<core::ResourceManager> CreateResourceManager(const core::Options &options) {
  // Short-circuit: if user didn't ask for any GPU load, skip vendor probing
  // entirely (avoids dlopen of CUDA driver on machines that have it but
  // don't want loadmaster to touch the GPU).
  if (options.GetGpuLoad() <= 0 && options.GetGpuMemoryBytes() <= 0) {
    return nullptr;
  }

  std::vector<IndexedDevice> devices;
  switch (options.GetGpuVendor()) {
    case core::Options::GpuVendor::kNvidia:
      devices = EnumerateNvidia(options);
      break;
    case core::Options::GpuVendor::kAmd:
      devices = EnumerateAmd(options);
      break;
    case core::Options::GpuVendor::kAuto:
      // Prefer NVIDIA, fall back to AMD. Mixed-vendor hosts are rare and
      // intentionally out of scope for now.
      devices = EnumerateNvidia(options);
      if (devices.empty()) {
        devices = EnumerateAmd(options);
      }
      break;
  }

  if (devices.empty()) {
    LOG_WARN("GPU module: no matching device found, disabling");
    return nullptr;
  }
  return std::make_unique<GpuResourceManager>(options, std::move(devices));
}

}  // namespace gpu
