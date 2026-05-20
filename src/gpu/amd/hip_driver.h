#pragma once

// Minimal subset of the HIP runtime + hipRTC loaded at runtime via dlopen.
// We don't include hip/hip_runtime.h so the project compiles without ROCm
// installed.

#include <cstddef>
#include <cstdint>

namespace gpu::amd {

using hipError_t = int;
using hipDevice_t = int;
using hipDeviceptr_t = void *;
using hipModule_t = void *;
using hipFunction_t = void *;
using hipStream_t = void *;
using hipCtx_t = void *;

using hiprtcResult = int;
using hiprtcProgram = void *;

constexpr hipError_t hipSuccess = 0;
constexpr hiprtcResult HIPRTC_SUCCESS = 0;

struct HipApi {
  // libamdhip64
  hipError_t (*hipInit)(unsigned int flags);
  hipError_t (*hipGetDeviceCount)(int *count);
  hipError_t (*hipDeviceGet)(hipDevice_t *device, int ordinal);
  hipError_t (*hipDeviceGetName)(char *name, int len, hipDevice_t dev);
  hipError_t (*hipCtxCreate)(hipCtx_t *pctx, unsigned int flags, hipDevice_t dev);
  hipError_t (*hipCtxDestroy)(hipCtx_t ctx);
  hipError_t (*hipCtxSetCurrent)(hipCtx_t ctx);
  hipError_t (*hipCtxSynchronize)();
  hipError_t (*hipModuleLoadData)(hipModule_t *module, const void *image);
  hipError_t (*hipModuleUnload)(hipModule_t hmod);
  hipError_t (*hipModuleGetFunction)(hipFunction_t *hfunc, hipModule_t hmod, const char *name);
  hipError_t (*hipModuleLaunchKernel)(hipFunction_t f, unsigned int gridDimX, unsigned int gridDimY,
                                      unsigned int gridDimZ, unsigned int blockDimX,
                                      unsigned int blockDimY, unsigned int blockDimZ,
                                      unsigned int sharedMemBytes, hipStream_t hStream,
                                      void **kernelParams, void **extra);
  hipError_t (*hipMalloc)(void **ptr, size_t size);
  hipError_t (*hipFree)(void *ptr);
  hipError_t (*hipMemset)(void *dst, int value, size_t sizeBytes);
  const char *(*hipGetErrorString)(hipError_t err);

  // libhiprtc
  hiprtcResult (*hiprtcCreateProgram)(hiprtcProgram *prog, const char *src, const char *name,
                                      int numHeaders, const char **headers,
                                      const char **includeNames);
  hiprtcResult (*hiprtcDestroyProgram)(hiprtcProgram *prog);
  hiprtcResult (*hiprtcCompileProgram)(hiprtcProgram prog, int numOptions, const char **options);
  hiprtcResult (*hiprtcGetCodeSize)(hiprtcProgram prog, size_t *codeSizeRet);
  hiprtcResult (*hiprtcGetCode)(hiprtcProgram prog, char *code);
  hiprtcResult (*hiprtcGetProgramLogSize)(hiprtcProgram prog, size_t *logSizeRet);
  hiprtcResult (*hiprtcGetProgramLog)(hiprtcProgram prog, char *log);
  const char *(*hiprtcGetErrorString)(hiprtcResult result);
};

const HipApi *LoadHipApi();

const char *HipErrorString(hipError_t err);
const char *HiprtcErrorString(hiprtcResult err);

}  // namespace gpu::amd
