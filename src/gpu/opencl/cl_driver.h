#pragma once

// Minimal subset of the OpenCL 1.2 API loaded at runtime via dlopen /
// LoadLibrary. Header-self-contained: we deliberately do NOT include
// <CL/cl.h> so the project builds without the OpenCL SDK installed
// (only the vendor / Khronos ICD loader, OpenCL.dll / libOpenCL.so.1,
// has to be present at *runtime*).
//
// Sizes/values mirror the Khronos OpenCL 1.2 ABI; the loader on Windows
// is shipped by the OS, on Linux by ocl-icd / khronos-opencl-loader.

#include <cstddef>
#include <cstdint>

#include "core/platform.h"

// OpenCL callbacks use stdcall on Windows (CL_CALLBACK == __stdcall in
// the official headers) and the platform default everywhere else. We
// expose the same convention here so the function-pointer table is ABI-
// compatible with the loader on each platform.
#if IS_WINDOWS
#  define LOADMASTER_CL_CALLBACK __stdcall
#else
#  define LOADMASTER_CL_CALLBACK
#endif

namespace gpu::opencl {

// ---- Opaque handles ------------------------------------------------------
using cl_int = int32_t;
using cl_uint = uint32_t;
using cl_ulong = uint64_t;
using cl_bitfield = uint64_t;
using cl_platform_id = void *;
using cl_device_id = void *;
using cl_context = void *;
using cl_command_queue = void *;
using cl_mem = void *;
using cl_program = void *;
using cl_kernel = void *;
using cl_event = void *;

// ---- Result codes (only the ones we check) -------------------------------
constexpr cl_int CL_SUCCESS = 0;
constexpr cl_int CL_BUILD_PROGRAM_FAILURE = -11;

// ---- Bitfields / enums ---------------------------------------------------
constexpr cl_bitfield CL_DEVICE_TYPE_GPU = 1ull << 2;
constexpr cl_bitfield CL_MEM_READ_WRITE = 1ull << 0;

// clGetDeviceInfo / clGetPlatformInfo / clGetProgramBuildInfo selectors
constexpr cl_uint CL_DEVICE_NAME = 0x102B;
constexpr cl_uint CL_DEVICE_VENDOR = 0x102C;
constexpr cl_uint CL_PLATFORM_NAME = 0x0902;
constexpr cl_uint CL_PROGRAM_BUILD_LOG = 0x1183;

// ---- Function pointer table ---------------------------------------------
struct ClApi {
  cl_int (*clGetPlatformIDs)(cl_uint num_entries, cl_platform_id *platforms,
                             cl_uint *num_platforms);
  cl_int (*clGetPlatformInfo)(cl_platform_id platform, cl_uint param_name, std::size_t param_size,
                              void *param_value, std::size_t *param_size_ret);
  cl_int (*clGetDeviceIDs)(cl_platform_id platform, cl_bitfield device_type, cl_uint num_entries,
                           cl_device_id *devices, cl_uint *num_devices);
  cl_int (*clGetDeviceInfo)(cl_device_id device, cl_uint param_name, std::size_t param_size,
                            void *param_value, std::size_t *param_size_ret);

  cl_context (*clCreateContext)(const intptr_t *properties, cl_uint num_devices,
                                const cl_device_id *devices,
                                void(LOADMASTER_CL_CALLBACK *pfn_notify)(const char *,
                                                                          const void *, size_t,
                                                                          void *),
                                void *user_data, cl_int *errcode_ret);
  cl_int (*clReleaseContext)(cl_context context);

  cl_command_queue (*clCreateCommandQueue)(cl_context context, cl_device_id device,
                                           cl_bitfield properties, cl_int *errcode_ret);
  cl_int (*clReleaseCommandQueue)(cl_command_queue queue);
  cl_int (*clFinish)(cl_command_queue queue);

  cl_mem (*clCreateBuffer)(cl_context context, cl_bitfield flags, std::size_t size, void *host_ptr,
                           cl_int *errcode_ret);
  cl_int (*clReleaseMemObject)(cl_mem memobj);
  cl_int (*clEnqueueFillBuffer)(cl_command_queue queue, cl_mem buffer, const void *pattern,
                                std::size_t pattern_size, std::size_t offset, std::size_t size,
                                cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
                                cl_event *event);

  cl_program (*clCreateProgramWithSource)(cl_context context, cl_uint count, const char **strings,
                                          const std::size_t *lengths, cl_int *errcode_ret);
  cl_int (*clBuildProgram)(cl_program program, cl_uint num_devices, const cl_device_id *device_list,
                           const char *options,
                           void(LOADMASTER_CL_CALLBACK *pfn_notify)(cl_program, void *),
                           void *user_data);
  cl_int (*clGetProgramBuildInfo)(cl_program program, cl_device_id device, cl_uint param_name,
                                  std::size_t param_size, void *param_value,
                                  std::size_t *param_size_ret);
  cl_int (*clReleaseProgram)(cl_program program);

  cl_kernel (*clCreateKernel)(cl_program program, const char *kernel_name, cl_int *errcode_ret);
  cl_int (*clReleaseKernel)(cl_kernel kernel);
  cl_int (*clSetKernelArg)(cl_kernel kernel, cl_uint arg_index, std::size_t arg_size,
                           const void *arg_value);

  cl_int (*clEnqueueNDRangeKernel)(cl_command_queue queue, cl_kernel kernel, cl_uint work_dim,
                                   const std::size_t *global_work_offset,
                                   const std::size_t *global_work_size,
                                   const std::size_t *local_work_size,
                                   cl_uint num_events_in_wait_list,
                                   const cl_event *event_wait_list, cl_event *event);
};

// Returns a singleton API table; first call dlopens the OpenCL loader and
// resolves the symbols. Returns nullptr on any failure.
const ClApi *LoadClApi();

// Stringify an OpenCL error code; always returns a non-null C string.
const char *ClErrorString(cl_int err);

}  // namespace gpu::opencl
