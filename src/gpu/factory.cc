#include "factory.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "core/options.h"
#include "core/platform.h"
#include "util/log.h"

#include "amd/amd_device.h"
#include "amd/hip_driver.h"
#include "intel/intel_device.h"
#include "intel/spirv_kernel.h"
#include "intel/ze_driver.h"
#include "manager_default.h"
#include "manager_random_normal.h"
#include "nvidia/cuda_driver.h"
#include "nvidia/nvidia_device.h"
#include "opencl/cl_driver.h"
#include "opencl/opencl_device.h"

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

// Intel iGPU / Arc via Level Zero. Linux/Windows only; Apple has no L0.
std::vector<IndexedDevice> EnumerateIntel(const core::Options &options) {
  std::vector<IndexedDevice> out;
#if IS_MACOS
  (void)options;
  return out;
#else
  if (!intel::kBusyKernelSpirvAvailable) {
    // Build did not embed a SPIR-V kernel (busy_kernel.spv missing at
    // configure time). The Intel backend cannot run; skip the dlopen.
    LOG_INFO("Intel: skipped (no SPIR-V kernel embedded in this build)");
    return out;
  }
  const auto *api = intel::LoadZeApi();
  if (api == nullptr) {
    return out;
  }
  uint32_t driver_count = 0;
  if (api->zeDriverGet(&driver_count, nullptr) != intel::ZE_RESULT_SUCCESS || driver_count == 0) {
    LOG_INFO("Intel: no Level Zero drivers");
    return out;
  }
  std::vector<intel::ze_driver_handle_t> drivers(driver_count);
  if (api->zeDriverGet(&driver_count, drivers.data()) != intel::ZE_RESULT_SUCCESS) {
    return out;
  }
  int gpu_count = 0;
  for (auto drv : drivers) {
    uint32_t dev_count = 0;
    if (api->zeDeviceGet(drv, &dev_count, nullptr) != intel::ZE_RESULT_SUCCESS || dev_count == 0) {
      continue;
    }
    std::vector<intel::ze_device_handle_t> devs(dev_count);
    if (api->zeDeviceGet(drv, &dev_count, devs.data()) != intel::ZE_RESULT_SUCCESS) {
      continue;
    }
    for (auto d : devs) {
      intel::ze_device_properties_t props{};
      props.stype = intel::ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES;
      if (api->zeDeviceGetProperties(d, &props) != intel::ZE_RESULT_SUCCESS) {
        continue;
      }
      if (props.type != intel::ZE_DEVICE_TYPE_GPU) {
        continue;
      }
      ++gpu_count;
    }
  }
  if (gpu_count <= 0) {
    LOG_INFO("Intel: Level Zero loaded but no GPU devices reported");
    return out;
  }
  LOG_INFO("Intel: %d Level Zero GPU device(s) detected", gpu_count);
  for (int i = 0; i < gpu_count; ++i) {
    if (!DeviceWanted(options, i)) {
      continue;
    }
    out.emplace_back(i, std::make_unique<intel::IntelDevice>());
  }
  return out;
#endif
}

// Generic OpenCL backend -- covers Intel iGPU when L0 is missing, AMD
// APU iGPU (no ROCm needed), and serves as a universal last-resort.
std::vector<IndexedDevice> EnumerateOpenCl(const core::Options &options) {
  std::vector<IndexedDevice> out;
#if IS_MACOS
  // OpenCL is deprecated by Apple; we already have Metal. Skip without noise.
  (void)options;
  return out;
#else
  const int count = opencl::OpenClDevice::DeviceCount();
  if (count <= 0) {
    LOG_INFO("OpenCL: no GPU devices reported");
    return out;
  }
  LOG_INFO("OpenCL: %d GPU device(s) detected", count);
  for (int i = 0; i < count; ++i) {
    if (!DeviceWanted(options, i)) {
      continue;
    }
    out.emplace_back(i, std::make_unique<opencl::OpenClDevice>());
  }
  return out;
#endif
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
    case core::Options::GpuVendor::kIntel:
      devices = EnumerateIntel(options);
      break;
    case core::Options::GpuVendor::kOpenCL:
      devices = EnumerateOpenCl(options);
      break;
    case core::Options::GpuVendor::kAuto:
#if IS_MACOS
      // On macOS the only viable backend is Apple Metal -- NVIDIA / AMD
      // / Intel L0 / OpenCL stacks are not present, so probing them
      // adds nothing but noise.
      devices = EnumerateApple(options);
#else
      // Probe order: discrete vendors first (NVIDIA -> AMD HIP), then
      // Intel via Level Zero, then OpenCL as the universal fallback
      // that captures Intel iGPU on systems without L0 *and* AMD APU
      // iGPU (which ROCm can't drive). Mixed-vendor hosts intentionally
      // out of scope: we stop at the first backend that finds anything.
      devices = EnumerateNvidia(options);
      if (devices.empty()) {
        devices = EnumerateAmd(options);
      }
      if (devices.empty()) {
        devices = EnumerateIntel(options);
      }
      if (devices.empty()) {
        devices = EnumerateOpenCl(options);
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
