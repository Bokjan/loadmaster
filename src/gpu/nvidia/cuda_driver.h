#pragma once

// Minimal subset of the CUDA Driver API loaded at runtime via dlopen.
// Header-self-contained: we deliberately do NOT include <cuda.h> so the
// project builds without the CUDA Toolkit installed.

#include <cstddef>
#include <cstdint>

namespace gpu::nvidia {

// Mirror the handful of types we actually use. Sizes/layout match the
// public ABI of libcuda.so.1.
using CUresult = int;
using CUdevice = int;
using CUcontext = void *;
using CUmodule = void *;
using CUfunction = void *;
using CUstream = void *;
using CUdeviceptr = std::uintptr_t;

constexpr CUresult CUDA_SUCCESS = 0;

// Function pointer table. All pointers are nullptr until Load() succeeds.
struct CudaApi {
  CUresult (*cuInit)(unsigned int Flags);
  CUresult (*cuDeviceGetCount)(int *count);
  CUresult (*cuDeviceGet)(CUdevice *device, int ordinal);
  CUresult (*cuDeviceGetName)(char *name, int len, CUdevice dev);
  CUresult (*cuCtxCreate)(CUcontext *pctx, unsigned int flags, CUdevice dev);
  CUresult (*cuCtxDestroy)(CUcontext ctx);
  CUresult (*cuCtxSetCurrent)(CUcontext ctx);
  CUresult (*cuCtxSynchronize)();
  CUresult (*cuModuleLoadData)(CUmodule *module, const void *image);
  CUresult (*cuModuleUnload)(CUmodule hmod);
  CUresult (*cuModuleGetFunction)(CUfunction *hfunc, CUmodule hmod, const char *name);
  CUresult (*cuLaunchKernel)(CUfunction f, unsigned int gridDimX, unsigned int gridDimY,
                             unsigned int gridDimZ, unsigned int blockDimX, unsigned int blockDimY,
                             unsigned int blockDimZ, unsigned int sharedMemBytes, CUstream hStream,
                             void **kernelParams, void **extra);
  CUresult (*cuMemAlloc)(CUdeviceptr *dptr, size_t bytesize);
  CUresult (*cuMemFree)(CUdeviceptr dptr);
  CUresult (*cuMemsetD8)(CUdeviceptr dstDevice, unsigned char uc, size_t N);
  CUresult (*cuGetErrorString)(CUresult error, const char **pStr);
};

// Returns a singleton API table; first call dlopens libcuda.so.1 and
// resolves the symbols. Returns nullptr if the driver isn't present.
const CudaApi *LoadCudaApi();

// Convenience: stringify an error code via cuGetErrorString. Always returns
// a non-null C string ("<unknown>" on failure).
const char *CudaErrorString(CUresult err);

}  // namespace gpu::nvidia
