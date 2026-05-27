#include "cl_driver.h"

#include <cstring>
#include <mutex>

#include "core/platform.h"
#include "util/dl.h"
#include "util/log.h"

namespace gpu::opencl {

namespace {

ClApi g_api{};
bool g_loaded = false;
bool g_load_attempted = false;
std::mutex g_load_mutex;
util::DlHandle g_handle = nullptr;

template <typename Fn>
bool Resolve(util::DlHandle handle, const char *name, Fn &slot) {
  void *sym = util::Dlsym(handle, name);
  if (sym == nullptr) {
    LOG_WARN("Dlsym(OpenCL, %s) failed: %s", name, util::Dlerror());
    return false;
  }
  slot = reinterpret_cast<Fn>(sym);
  return true;
}

bool DoLoad() {
  // Library candidates by platform.
  //   * Linux:   libOpenCL.so.1 from ocl-icd / khronos-opencl-loader.
  //   * Windows: OpenCL.dll, shipped with the OS in C:\Windows\System32
  //              (Windows 10+) and forwarded by the system to whichever
  //              vendor ICDs are registered.
  static const char *const kCandidates[] = {
#if IS_WINDOWS
      "OpenCL.dll",
#else
      "libOpenCL.so.1",
      "libOpenCL.so",
#endif
      nullptr,
  };

  g_handle = util::DlopenAny(kCandidates);
  if (g_handle == nullptr) {
    LOG_INFO("OpenCL loader shared library not available: %s", util::Dlerror());
    return false;
  }

  bool ok = true;
  ok &= Resolve(g_handle, "clGetPlatformIDs", g_api.clGetPlatformIDs);
  ok &= Resolve(g_handle, "clGetPlatformInfo", g_api.clGetPlatformInfo);
  ok &= Resolve(g_handle, "clGetDeviceIDs", g_api.clGetDeviceIDs);
  ok &= Resolve(g_handle, "clGetDeviceInfo", g_api.clGetDeviceInfo);
  ok &= Resolve(g_handle, "clCreateContext", g_api.clCreateContext);
  ok &= Resolve(g_handle, "clReleaseContext", g_api.clReleaseContext);
  // clCreateCommandQueue is a 1.2-and-earlier symbol; on 2.0+ ICDs it is
  // still exported alongside clCreateCommandQueueWithProperties so this
  // resolve succeeds on every modern driver we care about.
  ok &= Resolve(g_handle, "clCreateCommandQueue", g_api.clCreateCommandQueue);
  ok &= Resolve(g_handle, "clReleaseCommandQueue", g_api.clReleaseCommandQueue);
  ok &= Resolve(g_handle, "clFinish", g_api.clFinish);
  ok &= Resolve(g_handle, "clCreateBuffer", g_api.clCreateBuffer);
  ok &= Resolve(g_handle, "clReleaseMemObject", g_api.clReleaseMemObject);
  ok &= Resolve(g_handle, "clEnqueueFillBuffer", g_api.clEnqueueFillBuffer);
  ok &= Resolve(g_handle, "clCreateProgramWithSource", g_api.clCreateProgramWithSource);
  ok &= Resolve(g_handle, "clBuildProgram", g_api.clBuildProgram);
  ok &= Resolve(g_handle, "clGetProgramBuildInfo", g_api.clGetProgramBuildInfo);
  ok &= Resolve(g_handle, "clReleaseProgram", g_api.clReleaseProgram);
  ok &= Resolve(g_handle, "clCreateKernel", g_api.clCreateKernel);
  ok &= Resolve(g_handle, "clReleaseKernel", g_api.clReleaseKernel);
  ok &= Resolve(g_handle, "clSetKernelArg", g_api.clSetKernelArg);
  ok &= Resolve(g_handle, "clEnqueueNDRangeKernel", g_api.clEnqueueNDRangeKernel);

  if (!ok) {
    LOG_WARN("failed to resolve all OpenCL symbols");
    util::Dlclose(g_handle);
    g_handle = nullptr;
    std::memset(&g_api, 0, sizeof(g_api));
    return false;
  }

  // Probe: at least one platform must be visible; otherwise the loader
  // is present but no ICD is registered (e.g. headless Linux server with
  // ocl-icd installed but no GPU driver). Fail closed so the factory can
  // fall through.
  cl_uint platform_count = 0;
  if (g_api.clGetPlatformIDs(0, nullptr, &platform_count) != CL_SUCCESS || platform_count == 0) {
    LOG_INFO("OpenCL loader present but no ICD platforms registered");
    util::Dlclose(g_handle);
    g_handle = nullptr;
    std::memset(&g_api, 0, sizeof(g_api));
    return false;
  }
  return true;
}

}  // namespace

const ClApi *LoadClApi() {
  std::lock_guard<std::mutex> lock(g_load_mutex);
  if (g_load_attempted) {
    return g_loaded ? &g_api : nullptr;
  }
  g_load_attempted = true;
  g_loaded = DoLoad();
  return g_loaded ? &g_api : nullptr;
}

const char *ClErrorString(cl_int err) {
  // Khronos doesn't provide an official err-to-string helper in the
  // loader ABI; the values below are stable across versions.
  switch (err) {
    case 0:
      return "CL_SUCCESS";
    case -1:
      return "CL_DEVICE_NOT_FOUND";
    case -2:
      return "CL_DEVICE_NOT_AVAILABLE";
    case -3:
      return "CL_COMPILER_NOT_AVAILABLE";
    case -4:
      return "CL_MEM_OBJECT_ALLOCATION_FAILURE";
    case -5:
      return "CL_OUT_OF_RESOURCES";
    case -6:
      return "CL_OUT_OF_HOST_MEMORY";
    case -11:
      return "CL_BUILD_PROGRAM_FAILURE";
    case -30:
      return "CL_INVALID_VALUE";
    case -34:
      return "CL_INVALID_CONTEXT";
    case -36:
      return "CL_INVALID_COMMAND_QUEUE";
    case -38:
      return "CL_INVALID_MEM_OBJECT";
    case -45:
      return "CL_INVALID_PROGRAM_EXECUTABLE";
    case -48:
      return "CL_INVALID_KERNEL";
    case -49:
      return "CL_INVALID_ARG_INDEX";
    case -50:
      return "CL_INVALID_ARG_VALUE";
    case -51:
      return "CL_INVALID_ARG_SIZE";
    case -52:
      return "CL_INVALID_KERNEL_ARGS";
    case -54:
      return "CL_INVALID_WORK_GROUP_SIZE";
    default:
      return "<opencl error>";
  }
}

}  // namespace gpu::opencl
