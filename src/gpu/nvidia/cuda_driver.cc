#include "cuda_driver.h"

#include <cstring>
#include <mutex>

#include "core/platform.h"
#include "util/dl.h"
#include "util/log.h"

namespace gpu::nvidia {

namespace {

CudaApi g_api{};
bool g_loaded = false;
bool g_load_attempted = false;
std::mutex g_load_mutex;
util::DlHandle g_handle = nullptr;

template <typename Fn>
bool Resolve(util::DlHandle handle, const char *name, Fn &slot) {
  void *sym = util::Dlsym(handle, name);
  if (sym == nullptr) {
    LOG_WARN("Dlsym(libcuda, %s) failed: %s", name, util::Dlerror());
    return false;
  }
  slot = reinterpret_cast<Fn>(sym);
  return true;
}

bool DoLoad() {
  // Library candidates by platform.
  //   * Linux:   libcuda.so.1 ships with the NVIDIA driver; .so as a
  //              fallback for unusual distributions.
  //   * Windows: nvcuda.dll lives in C:\Windows\System32 and is in the
  //              default DLL search path with a stock NVIDIA driver
  //              install.
  static const char *const kCandidates[] = {
#if IS_WINDOWS
      "nvcuda.dll",
#else
      "libcuda.so.1",
      "libcuda.so",
#endif
      nullptr,
  };

  g_handle = util::DlopenAny(kCandidates);
  if (g_handle == nullptr) {
    LOG_INFO("CUDA driver shared library not available: %s", util::Dlerror());
    return false;
  }

  bool ok = true;
  ok &= Resolve(g_handle, "cuInit", g_api.cuInit);
  ok &= Resolve(g_handle, "cuDeviceGetCount", g_api.cuDeviceGetCount);
  ok &= Resolve(g_handle, "cuDeviceGet", g_api.cuDeviceGet);
  ok &= Resolve(g_handle, "cuDeviceGetName", g_api.cuDeviceGetName);
  ok &= Resolve(g_handle, "cuCtxCreate_v2", g_api.cuCtxCreate);
  ok &= Resolve(g_handle, "cuCtxDestroy_v2", g_api.cuCtxDestroy);
  ok &= Resolve(g_handle, "cuCtxSetCurrent", g_api.cuCtxSetCurrent);
  ok &= Resolve(g_handle, "cuCtxSynchronize", g_api.cuCtxSynchronize);
  ok &= Resolve(g_handle, "cuModuleLoadData", g_api.cuModuleLoadData);
  ok &= Resolve(g_handle, "cuModuleUnload", g_api.cuModuleUnload);
  ok &= Resolve(g_handle, "cuModuleGetFunction", g_api.cuModuleGetFunction);
  ok &= Resolve(g_handle, "cuLaunchKernel", g_api.cuLaunchKernel);
  ok &= Resolve(g_handle, "cuMemAlloc_v2", g_api.cuMemAlloc);
  ok &= Resolve(g_handle, "cuMemFree_v2", g_api.cuMemFree);
  ok &= Resolve(g_handle, "cuMemsetD8_v2", g_api.cuMemsetD8);
  ok &= Resolve(g_handle, "cuGetErrorString", g_api.cuGetErrorString);

  if (!ok) {
    LOG_WARN("failed to resolve all CUDA driver symbols");
    util::Dlclose(g_handle);
    g_handle = nullptr;
    return false;
  }

  const CUresult init_rc = g_api.cuInit(0);
  if (init_rc != CUDA_SUCCESS) {
    LOG_INFO("cuInit failed (rc=%d), no usable NVIDIA GPU", init_rc);
    util::Dlclose(g_handle);
    g_handle = nullptr;
    std::memset(&g_api, 0, sizeof(g_api));
    return false;
  }
  return true;
}

}  // namespace

const CudaApi *LoadCudaApi() {
  std::lock_guard<std::mutex> lock(g_load_mutex);
  if (g_load_attempted) {
    return g_loaded ? &g_api : nullptr;
  }
  g_load_attempted = true;
  g_loaded = DoLoad();
  return g_loaded ? &g_api : nullptr;
}

const char *CudaErrorString(CUresult err) {
  if (!g_loaded || g_api.cuGetErrorString == nullptr) {
    return "<cuda driver not loaded>";
  }
  const char *s = nullptr;
  if (g_api.cuGetErrorString(err, &s) != CUDA_SUCCESS || s == nullptr) {
    return "<unknown>";
  }
  return s;
}

}  // namespace gpu::nvidia
