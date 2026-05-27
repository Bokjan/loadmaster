#include "opencl_device.h"

#include <chrono>
#include <cstdio>
#include <cstring>

#include "cl_kernel.h"

#include "gpu/constants.h"
#include "util/log.h"

namespace gpu::opencl {

OpenClDevice::OpenClDevice()
    : api_(nullptr),
      platform_(nullptr),
      device_(nullptr),
      context_(nullptr),
      queue_(nullptr),
      program_(nullptr),
      kernel_(nullptr),
      out_buf_(nullptr),
      out_bytes_(0),
      mem_load_buf_(nullptr),
      mem_load_bytes_(0),
      device_index_(-1) {}

OpenClDevice::~OpenClDevice() { Destroy(); }

void OpenClDevice::Destroy() {
  if (api_ == nullptr) {
    return;
  }
  if (mem_load_buf_ != nullptr) {
    api_->clReleaseMemObject(mem_load_buf_);
    mem_load_buf_ = nullptr;
    mem_load_bytes_ = 0;
  }
  if (out_buf_ != nullptr) {
    api_->clReleaseMemObject(out_buf_);
    out_buf_ = nullptr;
    out_bytes_ = 0;
  }
  if (kernel_ != nullptr) {
    api_->clReleaseKernel(kernel_);
    kernel_ = nullptr;
  }
  if (program_ != nullptr) {
    api_->clReleaseProgram(program_);
    program_ = nullptr;
  }
  if (queue_ != nullptr) {
    api_->clReleaseCommandQueue(queue_);
    queue_ = nullptr;
  }
  if (context_ != nullptr) {
    api_->clReleaseContext(context_);
    context_ = nullptr;
  }
}

std::vector<std::pair<cl_platform_id, cl_device_id>> OpenClDevice::CollectGpuDevices(
    const ClApi *api) {
  std::vector<std::pair<cl_platform_id, cl_device_id>> out;
  if (api == nullptr) {
    return out;
  }
  cl_uint platform_count = 0;
  if (api->clGetPlatformIDs(0, nullptr, &platform_count) != CL_SUCCESS || platform_count == 0) {
    return out;
  }
  std::vector<cl_platform_id> platforms(platform_count);
  if (api->clGetPlatformIDs(platform_count, platforms.data(), nullptr) != CL_SUCCESS) {
    return out;
  }
  for (auto p : platforms) {
    cl_uint dev_count = 0;
    if (api->clGetDeviceIDs(p, CL_DEVICE_TYPE_GPU, 0, nullptr, &dev_count) != CL_SUCCESS ||
        dev_count == 0) {
      continue;
    }
    std::vector<cl_device_id> devs(dev_count);
    if (api->clGetDeviceIDs(p, CL_DEVICE_TYPE_GPU, dev_count, devs.data(), nullptr) != CL_SUCCESS) {
      continue;
    }
    for (auto d : devs) {
      out.emplace_back(p, d);
    }
  }
  return out;
}

int OpenClDevice::DeviceCount() {
  const auto *api = LoadClApi();
  if (api == nullptr) {
    return 0;
  }
  return static_cast<int>(CollectGpuDevices(api).size());
}

bool OpenClDevice::Init(int device_index) {
  api_ = LoadClApi();
  if (api_ == nullptr) {
    return false;
  }
  device_index_ = device_index;

  const auto pairs = CollectGpuDevices(api_);
  if (pairs.empty()) {
    LOG_INFO("OpenCL: no GPU devices reported");
    return false;
  }
  if (device_index < 0 || static_cast<size_t>(device_index) >= pairs.size()) {
    LOG_WARN("OpenCL: device index %d out of range (have %zu)", device_index, pairs.size());
    return false;
  }
  platform_ = pairs[device_index].first;
  device_ = pairs[device_index].second;

  // Compose a friendly name "<platform> / <device>" so users can tell
  // apart two ICDs covering the same physical GPU.
  char dev_name[256] = {0};
  char plat_name[128] = {0};
  api_->clGetDeviceInfo(device_, CL_DEVICE_NAME, sizeof(dev_name) - 1, dev_name, nullptr);
  api_->clGetPlatformInfo(platform_, CL_PLATFORM_NAME, sizeof(plat_name) - 1, plat_name, nullptr);
  name_.clear();
  if (plat_name[0] != '\0') {
    name_.append(plat_name);
    name_.append(" / ");
  }
  name_.append(dev_name[0] != '\0' ? dev_name : "OpenCL GPU");

  cl_int rc = CL_SUCCESS;
  context_ = api_->clCreateContext(nullptr, 1, &device_, nullptr, nullptr, &rc);
  if (rc != CL_SUCCESS || context_ == nullptr) {
    LOG_WARN("clCreateContext failed: %s (rc=%d)", ClErrorString(rc), rc);
    return false;
  }
  queue_ = api_->clCreateCommandQueue(context_, device_, 0, &rc);
  if (rc != CL_SUCCESS || queue_ == nullptr) {
    LOG_WARN("clCreateCommandQueue failed: %s (rc=%d)", ClErrorString(rc), rc);
    Destroy();
    return false;
  }

  // Build the embedded kernel source.
  const char *src = kBusyKernelClSrc;
  program_ = api_->clCreateProgramWithSource(context_, 1, &src, nullptr, &rc);
  if (rc != CL_SUCCESS || program_ == nullptr) {
    LOG_WARN("clCreateProgramWithSource failed: %s (rc=%d)", ClErrorString(rc), rc);
    Destroy();
    return false;
  }
  rc = api_->clBuildProgram(program_, 1, &device_, nullptr, nullptr, nullptr);
  if (rc != CL_SUCCESS) {
    // Pull the build log to make compile errors actionable.
    size_t log_size = 0;
    if (api_->clGetProgramBuildInfo(program_, device_, CL_PROGRAM_BUILD_LOG, 0, nullptr,
                                    &log_size) == CL_SUCCESS &&
        log_size > 1) {
      std::vector<char> log(log_size);
      api_->clGetProgramBuildInfo(program_, device_, CL_PROGRAM_BUILD_LOG, log_size, log.data(),
                                  nullptr);
      LOG_WARN("clBuildProgram failed: %s\n%s", ClErrorString(rc), log.data());
    } else {
      LOG_WARN("clBuildProgram failed: %s (rc=%d)", ClErrorString(rc), rc);
    }
    Destroy();
    return false;
  }
  kernel_ = api_->clCreateKernel(program_, "busy", &rc);
  if (rc != CL_SUCCESS || kernel_ == nullptr) {
    LOG_WARN("clCreateKernel(busy) failed: %s (rc=%d)", ClErrorString(rc), rc);
    Destroy();
    return false;
  }

  // Per-thread scratch buffer.
  const size_t threads =
      static_cast<size_t>(kGpuKernelGridSize) * static_cast<size_t>(kGpuKernelBlockSize);
  out_bytes_ = threads * sizeof(uint32_t);
  out_buf_ = api_->clCreateBuffer(context_, CL_MEM_READ_WRITE, out_bytes_, nullptr, &rc);
  if (rc != CL_SUCCESS || out_buf_ == nullptr) {
    LOG_WARN("clCreateBuffer(scratch %zu) failed: %s (rc=%d)", out_bytes_, ClErrorString(rc), rc);
    Destroy();
    return false;
  }
  rc = api_->clSetKernelArg(kernel_, 0, sizeof(cl_mem), &out_buf_);
  if (rc != CL_SUCCESS) {
    LOG_WARN("clSetKernelArg(out) failed: %s (rc=%d)", ClErrorString(rc), rc);
    Destroy();
    return false;
  }
  return true;
}

std::string OpenClDevice::Name() const {
  char buf[384];
  std::snprintf(buf, sizeof(buf), "%s (#%d)", name_.c_str(), device_index_);
  return std::string(buf);
}

bool OpenClDevice::AllocateMemory(std::size_t bytes) {
  if (api_ == nullptr || context_ == nullptr || queue_ == nullptr) {
    return false;
  }
  if (mem_load_buf_ != nullptr) {
    api_->clReleaseMemObject(mem_load_buf_);
    mem_load_buf_ = nullptr;
    mem_load_bytes_ = 0;
  }
  cl_int rc = CL_SUCCESS;
  mem_load_buf_ = api_->clCreateBuffer(context_, CL_MEM_READ_WRITE, bytes, nullptr, &rc);
  if (rc != CL_SUCCESS || mem_load_buf_ == nullptr) {
    LOG_WARN("clCreateBuffer(%zu) failed: %s (rc=%d)", bytes, ClErrorString(rc), rc);
    mem_load_buf_ = nullptr;
    return false;
  }
  // Force backing-store commit by filling the buffer once.
  const uint8_t pattern = 0xA5;
  rc = api_->clEnqueueFillBuffer(queue_, mem_load_buf_, &pattern, sizeof(pattern), 0, bytes, 0,
                                 nullptr, nullptr);
  if (rc != CL_SUCCESS) {
    // Some old ICDs may not implement clEnqueueFillBuffer well; the
    // alloc still succeeded so we don't tear it down, just warn.
    LOG_DEBUG("clEnqueueFillBuffer failed: %s (rc=%d)", ClErrorString(rc), rc);
  }
  api_->clFinish(queue_);
  mem_load_bytes_ = bytes;
  return true;
}

void OpenClDevice::ReleaseMemory() {
  if (api_ == nullptr || mem_load_buf_ == nullptr) {
    return;
  }
  api_->clReleaseMemObject(mem_load_buf_);
  mem_load_buf_ = nullptr;
  mem_load_bytes_ = 0;
}

bool OpenClDevice::RunKernel(int loop_count) {
  if (api_ == nullptr || kernel_ == nullptr || queue_ == nullptr) {
    return false;
  }
  if (loop_count < 1) {
    loop_count = 1;
  }
  cl_int rc = api_->clSetKernelArg(kernel_, 1, sizeof(int), &loop_count);
  if (rc != CL_SUCCESS) {
    LOG_WARN("clSetKernelArg(loop) failed: %s (rc=%d)", ClErrorString(rc), rc);
    return false;
  }
  const size_t global_size = static_cast<size_t>(kGpuKernelGridSize) *
                             static_cast<size_t>(kGpuKernelBlockSize);
  const size_t local_size = static_cast<size_t>(kGpuKernelBlockSize);
  rc = api_->clEnqueueNDRangeKernel(queue_, kernel_, 1, nullptr, &global_size, &local_size, 0,
                                    nullptr, nullptr);
  if (rc != CL_SUCCESS) {
    LOG_WARN("clEnqueueNDRangeKernel failed: %s (rc=%d)", ClErrorString(rc), rc);
    return false;
  }
  rc = api_->clFinish(queue_);
  if (rc != CL_SUCCESS) {
    LOG_WARN("clFinish failed: %s (rc=%d)", ClErrorString(rc), rc);
    return false;
  }
  return true;
}

int OpenClDevice::ProbeBaseLoopCount(int target_ms) {
  if (api_ == nullptr || kernel_ == nullptr) {
    return 1;
  }
  int low = 1;
  int high = 1024;
  for (int probe = 0; probe < 24; ++probe) {
    const auto start = std::chrono::high_resolution_clock::now();
    if (!RunKernel(high)) {
      return std::max(1, low);
    }
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::high_resolution_clock::now() - start)
                                .count();
    if (elapsed_ms >= target_ms) {
      break;
    }
    low = high;
    if (high >= kGpuKernelLoopBaseMax / 2) {
      high = kGpuKernelLoopBaseMax;
      break;
    }
    high *= 2;
  }

  int best = low;
  int64_t best_diff = static_cast<int64_t>(target_ms);
  for (int i = 0; i < kGpuKernelLoopBaseTestIteration && low + 1 < high; ++i) {
    const int mid = low + (high - low) / 2;
    const auto start = std::chrono::high_resolution_clock::now();
    if (!RunKernel(mid)) {
      break;
    }
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::high_resolution_clock::now() - start)
                                .count();
    const int64_t diff =
        elapsed_ms > target_ms ? (elapsed_ms - target_ms) : (target_ms - elapsed_ms);
    if (diff < best_diff) {
      best = mid;
      best_diff = diff;
    }
    if (elapsed_ms > target_ms) {
      high = mid;
    } else {
      low = mid;
    }
  }
  LOG_DEBUG("OpenClDevice #%d probed base_loop_count=%d (target=%dms)", device_index_, best,
            target_ms);
  return std::max(1, best);
}

}  // namespace gpu::opencl
