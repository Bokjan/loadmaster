#include "ze_driver.h"

#include <cstring>
#include <mutex>

#include "core/platform.h"
#include "util/dl.h"
#include "util/log.h"

namespace gpu::intel {

namespace {

ZeApi g_api{};
bool g_loaded = false;
bool g_load_attempted = false;
std::mutex g_load_mutex;
util::DlHandle g_handle = nullptr;

template <typename Fn>
bool Resolve(util::DlHandle handle, const char *name, Fn &slot) {
  void *sym = util::Dlsym(handle, name);
  if (sym == nullptr) {
    LOG_WARN("Dlsym(ze_loader, %s) failed: %s", name, util::Dlerror());
    return false;
  }
  slot = reinterpret_cast<Fn>(sym);
  return true;
}

bool DoLoad() {
  // Library candidates by platform.
  //   * Linux:   libze_loader.so.1 ships with the Intel compute-runtime
  //              (NEO) packages. .so as fallback for unusual distros.
  //              On WSL2 the Intel Level Zero WSL package puts a shim
  //              under /usr/lib/wsl/lib/, which is on the default
  //              loader search path inside WSL2 distributions.
  //   * Windows: ze_loader.dll lives in C:\Windows\System32 with any
  //              recent Intel Graphics Driver (DCH 2021+).
  static const char *const kCandidates[] = {
#if IS_WINDOWS
      "ze_loader.dll",
#else
      "libze_loader.so.1",
      "libze_loader.so",
#endif
      nullptr,
  };

  g_handle = util::DlopenAny(kCandidates);
  if (g_handle == nullptr) {
    LOG_INFO("Level Zero loader shared library not available: %s", util::Dlerror());
    return false;
  }

  bool ok = true;
  ok &= Resolve(g_handle, "zeInit", g_api.zeInit);
  ok &= Resolve(g_handle, "zeDriverGet", g_api.zeDriverGet);
  ok &= Resolve(g_handle, "zeDeviceGet", g_api.zeDeviceGet);
  ok &= Resolve(g_handle, "zeDeviceGetProperties", g_api.zeDeviceGetProperties);
  ok &= Resolve(g_handle, "zeContextCreate", g_api.zeContextCreate);
  ok &= Resolve(g_handle, "zeContextDestroy", g_api.zeContextDestroy);
  ok &= Resolve(g_handle, "zeCommandQueueCreate", g_api.zeCommandQueueCreate);
  ok &= Resolve(g_handle, "zeCommandQueueDestroy", g_api.zeCommandQueueDestroy);
  ok &= Resolve(g_handle, "zeCommandQueueExecuteCommandLists",
                g_api.zeCommandQueueExecuteCommandLists);
  ok &= Resolve(g_handle, "zeCommandQueueSynchronize", g_api.zeCommandQueueSynchronize);
  ok &= Resolve(g_handle, "zeCommandListCreate", g_api.zeCommandListCreate);
  ok &= Resolve(g_handle, "zeCommandListDestroy", g_api.zeCommandListDestroy);
  ok &= Resolve(g_handle, "zeCommandListClose", g_api.zeCommandListClose);
  ok &= Resolve(g_handle, "zeCommandListReset", g_api.zeCommandListReset);
  ok &= Resolve(g_handle, "zeCommandListAppendLaunchKernel", g_api.zeCommandListAppendLaunchKernel);
  ok &= Resolve(g_handle, "zeCommandListAppendMemoryFill", g_api.zeCommandListAppendMemoryFill);
  ok &= Resolve(g_handle, "zeModuleCreate", g_api.zeModuleCreate);
  ok &= Resolve(g_handle, "zeModuleDestroy", g_api.zeModuleDestroy);
  ok &= Resolve(g_handle, "zeModuleBuildLogDestroy", g_api.zeModuleBuildLogDestroy);
  ok &= Resolve(g_handle, "zeModuleBuildLogGetString", g_api.zeModuleBuildLogGetString);
  ok &= Resolve(g_handle, "zeKernelCreate", g_api.zeKernelCreate);
  ok &= Resolve(g_handle, "zeKernelDestroy", g_api.zeKernelDestroy);
  ok &= Resolve(g_handle, "zeKernelSetGroupSize", g_api.zeKernelSetGroupSize);
  ok &= Resolve(g_handle, "zeKernelSetArgumentValue", g_api.zeKernelSetArgumentValue);
  ok &= Resolve(g_handle, "zeMemAllocDevice", g_api.zeMemAllocDevice);
  ok &= Resolve(g_handle, "zeMemFree", g_api.zeMemFree);

  if (!ok) {
    LOG_WARN("failed to resolve all Level Zero symbols");
    util::Dlclose(g_handle);
    g_handle = nullptr;
    std::memset(&g_api, 0, sizeof(g_api));
    return false;
  }

  // Restrict initialization to GPU drivers; we don't care about CPU/FPGA
  // L0 drivers that may also be installed.
  const ze_result_t init_rc = g_api.zeInit(ZE_INIT_FLAG_GPU_ONLY);
  if (init_rc != ZE_RESULT_SUCCESS) {
    LOG_INFO("zeInit failed (rc=0x%x), no usable Intel GPU via Level Zero", init_rc);
    util::Dlclose(g_handle);
    g_handle = nullptr;
    std::memset(&g_api, 0, sizeof(g_api));
    return false;
  }
  return true;
}

}  // namespace

const ZeApi *LoadZeApi() {
  std::lock_guard<std::mutex> lock(g_load_mutex);
  if (g_load_attempted) {
    return g_loaded ? &g_api : nullptr;
  }
  g_load_attempted = true;
  g_loaded = DoLoad();
  return g_loaded ? &g_api : nullptr;
}

const char *ZeResultString(ze_result_t err) {
  // Level Zero doesn't expose a public string-from-result helper in the
  // loader ABI, so we map only the codes we expect to see and fall back
  // to a hex dump otherwise. Mirrors the spec values from ze_api.h.
  switch (err) {
    case ZE_RESULT_SUCCESS:
      return "ZE_RESULT_SUCCESS";
    case 0x70000001:
      return "ZE_RESULT_NOT_READY";
    case 0x70000002:
      return "ZE_RESULT_ERROR_UNINITIALIZED";
    case 0x70000003:
      return "ZE_RESULT_ERROR_DEVICE_LOST";
    case 0x70000004:
      return "ZE_RESULT_ERROR_INVALID_ARGUMENT";
    case 0x70000005:
      return "ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY";
    case 0x70000006:
      return "ZE_RESULT_ERROR_OUT_OF_DEVICE_MEMORY";
    case 0x70000007:
      return "ZE_RESULT_ERROR_MODULE_BUILD_FAILURE";
    case 0x70000010:
      return "ZE_RESULT_ERROR_INSUFFICIENT_PERMISSIONS";
    case 0x70000011:
      return "ZE_RESULT_ERROR_NOT_AVAILABLE";
    case 0x70000018:
      return "ZE_RESULT_ERROR_DEPENDENCY_UNAVAILABLE";
    case 0x78000001:
      return "ZE_RESULT_ERROR_UNKNOWN";
    default:
      return "<level zero error>";
  }
}

}  // namespace gpu::intel
