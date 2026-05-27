#include "hip_driver.h"

#include <cstdio>
#include <cstring>
#include <mutex>

#include "core/platform.h"
#include "util/dl.h"
#include "util/log.h"

#if !IS_WINDOWS
#  include <sys/stat.h>
#endif

namespace gpu::amd {

namespace {

HipApi g_api{};
bool g_loaded = false;
bool g_load_attempted = false;
std::mutex g_load_mutex;
util::DlHandle g_hip_handle = nullptr;
util::DlHandle g_rtc_handle = nullptr;

#if !IS_WINDOWS
// Detect WSL2. AMD's ROCm runtime currently has only narrow, preview-quality
// support inside WSL2 (a few RDNA3 SKUs on ROCm >= 6.1) -- newer cards (e.g.
// RDNA4 / RX 9070 XT) and most setups will load libamdhip64.so but fail at
// hipGetDeviceCount with HSA_STATUS_ERROR_OUT_OF_RESOURCES because the
// kernel-side bits (/dev/kfd, /dev/dri/render*) aren't exposed by the WSL
// virtualization layer. Rather than letting the user chase that error, we
// short-circuit with a clear diagnostic.
bool IsRunningInsideWsl() {
  struct stat st{};
  if (::stat("/proc/sys/fs/binfmt_misc/WSLInterop", &st) == 0) {
    return true;
  }
  if (FILE *fp = std::fopen("/proc/version", "r"); fp != nullptr) {
    char buf[512] = {0};
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, fp);
    std::fclose(fp);
    buf[n] = '\0';
    if (std::strstr(buf, "microsoft") != nullptr || std::strstr(buf, "WSL") != nullptr ||
        std::strstr(buf, "Microsoft") != nullptr) {
      return true;
    }
  }
  return false;
}
#endif

template <typename Fn>
bool Resolve(util::DlHandle handle, const char *name, Fn &slot, bool required = true) {
  void *sym = util::Dlsym(handle, name);
  if (sym == nullptr) {
    if (required) {
      LOG_WARN("Dlsym(%s) failed: %s", name, util::Dlerror());
    }
    return false;
  }
  slot = reinterpret_cast<Fn>(sym);
  return true;
}

bool DoLoad() {
#if !IS_WINDOWS
  if (IsRunningInsideWsl()) {
    LOG_INFO(
        "AMD GPU module disabled: ROCm/HIP is not usable under WSL2 for most "
        "configurations (especially RDNA4 cards). Run loadmaster on native "
        "Linux (or Windows with HIP SDK) to drive an AMD GPU.");
    return false;
  }
#endif

  // Library candidates by platform.
  //   * Linux:   ROCm ships libamdhip64.so.<ver> + libhiprtc.so.<ver>
  //   * Windows: HIP SDK for Windows ships amdhip64_<ver>.dll + hiprtc<ver>.dll
  //              and historically also plain amdhip64.dll. We try multiple
  //              versioned names that have shipped in HIP SDK 5.x and 6.x.
  static const char *const kHipCandidates[] = {
#if IS_WINDOWS
      "amdhip64_6.dll",
      "amdhip64_5.dll",
      "amdhip64.dll",
#else
      "libamdhip64.so",
      "libamdhip64.so.6",
      "libamdhip64.so.5",
#endif
      nullptr,
  };
  static const char *const kRtcCandidates[] = {
#if IS_WINDOWS
      "hiprtc0606.dll", "hiprtc0605.dll", "hiprtc0604.dll", "hiprtc0507.dll", "hiprtc.dll",
#else
      "libhiprtc.so",
      "libhiprtc.so.6",
      "libhiprtc.so.5",
#endif
      nullptr,
  };

  g_hip_handle = util::DlopenAny(kHipCandidates);
  if (g_hip_handle == nullptr) {
    LOG_INFO("HIP runtime shared library not available: %s", util::Dlerror());
    return false;
  }
  g_rtc_handle = util::DlopenAny(kRtcCandidates);
  if (g_rtc_handle == nullptr) {
    LOG_INFO("hipRTC shared library not available: %s", util::Dlerror());
    util::Dlclose(g_hip_handle);
    g_hip_handle = nullptr;
    return false;
  }

  bool ok = true;
  // hipInit is optional / deprecated in modern HIP runtimes -- the runtime
  // initializes lazily on the first real API call.
  Resolve(g_hip_handle, "hipInit", g_api.hipInit, /*required=*/false);
  ok &= Resolve(g_hip_handle, "hipGetDeviceCount", g_api.hipGetDeviceCount);
  ok &= Resolve(g_hip_handle, "hipDeviceGet", g_api.hipDeviceGet);
  ok &= Resolve(g_hip_handle, "hipDeviceGetName", g_api.hipDeviceGetName);
  ok &= Resolve(g_hip_handle, "hipCtxCreate", g_api.hipCtxCreate);
  ok &= Resolve(g_hip_handle, "hipCtxDestroy", g_api.hipCtxDestroy);
  ok &= Resolve(g_hip_handle, "hipCtxSetCurrent", g_api.hipCtxSetCurrent);
  ok &= Resolve(g_hip_handle, "hipCtxSynchronize", g_api.hipCtxSynchronize);
  ok &= Resolve(g_hip_handle, "hipModuleLoadData", g_api.hipModuleLoadData);
  ok &= Resolve(g_hip_handle, "hipModuleUnload", g_api.hipModuleUnload);
  ok &= Resolve(g_hip_handle, "hipModuleGetFunction", g_api.hipModuleGetFunction);
  ok &= Resolve(g_hip_handle, "hipModuleLaunchKernel", g_api.hipModuleLaunchKernel);
  ok &= Resolve(g_hip_handle, "hipMalloc", g_api.hipMalloc);
  ok &= Resolve(g_hip_handle, "hipFree", g_api.hipFree);
  ok &= Resolve(g_hip_handle, "hipMemset", g_api.hipMemset);
  ok &= Resolve(g_hip_handle, "hipGetErrorString", g_api.hipGetErrorString);

  ok &= Resolve(g_rtc_handle, "hiprtcCreateProgram", g_api.hiprtcCreateProgram);
  ok &= Resolve(g_rtc_handle, "hiprtcDestroyProgram", g_api.hiprtcDestroyProgram);
  ok &= Resolve(g_rtc_handle, "hiprtcCompileProgram", g_api.hiprtcCompileProgram);
  ok &= Resolve(g_rtc_handle, "hiprtcGetCodeSize", g_api.hiprtcGetCodeSize);
  ok &= Resolve(g_rtc_handle, "hiprtcGetCode", g_api.hiprtcGetCode);
  ok &= Resolve(g_rtc_handle, "hiprtcGetProgramLogSize", g_api.hiprtcGetProgramLogSize);
  ok &= Resolve(g_rtc_handle, "hiprtcGetProgramLog", g_api.hiprtcGetProgramLog);
  ok &= Resolve(g_rtc_handle, "hiprtcGetErrorString", g_api.hiprtcGetErrorString);

  if (!ok) {
    LOG_WARN("failed to resolve all HIP/hipRTC symbols");
    util::Dlclose(g_rtc_handle);
    util::Dlclose(g_hip_handle);
    g_rtc_handle = nullptr;
    g_hip_handle = nullptr;
    std::memset(&g_api, 0, sizeof(g_api));
    return false;
  }

  // Probe the runtime by asking how many devices it sees. HIP initializes
  // lazily on the first call.
  if (g_api.hipInit != nullptr) {
    (void)g_api.hipInit(0);
  }
  int probe_count = 0;
  const hipError_t probe_rc = g_api.hipGetDeviceCount(&probe_count);
  if (probe_rc != hipSuccess) {
    const char *msg =
        (g_api.hipGetErrorString != nullptr) ? g_api.hipGetErrorString(probe_rc) : "<unknown>";
    LOG_INFO("HIP runtime present but unusable (hipGetDeviceCount rc=%d: %s)", probe_rc,
             msg ? msg : "<unknown>");
    LOG_INFO(
        "Hint: on Linux check /dev/kfd permissions and that the GPU's gfx "
        "arch is supported; on Windows ensure HIP SDK is installed and on PATH.");
    return false;
  }
  if (probe_count <= 0) {
    LOG_INFO("HIP runtime reports 0 devices");
    return false;
  }
  LOG_DEBUG("HIP runtime probe: %d device(s) visible", probe_count);
  return true;
}

}  // namespace

const HipApi *LoadHipApi() {
  std::lock_guard<std::mutex> lock(g_load_mutex);
  if (g_load_attempted) {
    return g_loaded ? &g_api : nullptr;
  }
  g_load_attempted = true;
  g_loaded = DoLoad();
  return g_loaded ? &g_api : nullptr;
}

const char *HipErrorString(hipError_t err) {
  if (!g_loaded || g_api.hipGetErrorString == nullptr) {
    return "<hip not loaded>";
  }
  const char *s = g_api.hipGetErrorString(err);
  return (s != nullptr) ? s : "<unknown>";
}

const char *HiprtcErrorString(hiprtcResult err) {
  if (!g_loaded || g_api.hiprtcGetErrorString == nullptr) {
    return "<hiprtc not loaded>";
  }
  const char *s = g_api.hiprtcGetErrorString(err);
  return (s != nullptr) ? s : "<unknown>";
}

}  // namespace gpu::amd
