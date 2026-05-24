#include "factory.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "core/options.h"
#include "core/platform.h"
#include "util/log.h"

#include "amd/amd_device.h"
#include "amd/hip_driver.h"
#include "manager_default.h"
#include "manager_random_normal.h"
#include "nvidia/cuda_driver.h"
#include "nvidia/nvidia_device.h"

#if IS_MACOS
#  include "apple/metal_device.h"
#endif

namespace gpu {

namespace {

using IndexedDevice = std::pair<int, std::unique_ptr<GpuDevice>>;

// Does the user want device #idx? `-gi all` (or unset) selects everything;
// otherwise only indices listed explicitly are kept. Centralised so the
// three vendor-specific Enumerate functions don't each repeat the same
// linear scan.
bool DeviceWanted(const core::Options &options, int idx) {
  if (options.GpuUseAllDevices()) {
    return true;
  }
  const auto &wanted = options.GetGpuIndices();
  return std::find(wanted.begin(), wanted.end(), idx) != wanted.end();
}

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
  for (int i = 0; i < count; ++i) {
    if (!DeviceWanted(options, i)) {
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
  for (int i = 0; i < count; ++i) {
    if (!DeviceWanted(options, i)) {
      continue;
    }
    out.emplace_back(i, std::make_unique<amd::AmdDevice>());
  }
  return out;
}

std::vector<IndexedDevice> EnumerateApple(const core::Options &options) {
  std::vector<IndexedDevice> out;
#if IS_MACOS
  const int count = apple::AppleMetalDevice::DeviceCount();
  if (count <= 0) {
    LOG_INFO("Apple: no Metal devices reported");
    return out;
  }
  LOG_INFO("Apple: %d Metal device(s) detected", count);
  for (int i = 0; i < count; ++i) {
    if (!DeviceWanted(options, i)) {
      continue;
    }
    out.emplace_back(i, std::make_unique<apple::AppleMetalDevice>());
  }
#else
  (void)options;
  LOG_INFO("Apple GPU backend is only available on macOS builds");
#endif
  return out;
}

// Pick the manager subclass that matches the user's `-ga` choice. Both
// share the same Init/teardown plumbing (in the GpuResourceManager
// base); they differ only in how Schedule() drives per-tick load.
std::unique_ptr<core::ResourceManager> MakeManager(const core::Options &options,
                                                   std::vector<IndexedDevice> devices) {
  switch (options.GetGpuAlgorithm()) {
    case core::Options::GpuAlgorithm::kDefault:
      return std::make_unique<GpuResourceManagerDefault>(options, std::move(devices));
    case core::Options::GpuAlgorithm::kRandomNormal:
      return std::make_unique<GpuResourceManagerRandomNormal>(options, std::move(devices));
  }
  // Unreachable: Options validates the algorithm at parse time.
  LOG_FATAL("invalid GPU algorithm type [%d]", static_cast<int>(options.GetGpuAlgorithm()));
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
    case core::Options::GpuVendor::kApple:
      devices = EnumerateApple(options);
      break;
    case core::Options::GpuVendor::kAuto:
#if IS_MACOS
      // On macOS the only viable backend is Apple Metal -- NVIDIA/AMD
      // shared libraries are simply not present, so probing them adds
      // nothing but noise.
      devices = EnumerateApple(options);
#else
      // Prefer NVIDIA, fall back to AMD. Mixed-vendor hosts are rare and
      // intentionally out of scope for now.
      devices = EnumerateNvidia(options);
      if (devices.empty()) {
        devices = EnumerateAmd(options);
      }
#endif
      break;
  }

  if (devices.empty()) {
    LOG_WARN("GPU module: no matching device found, disabling");
    return nullptr;
  }
  return MakeManager(options, std::move(devices));
}

}  // namespace gpu
