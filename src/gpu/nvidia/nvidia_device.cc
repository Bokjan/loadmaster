#include "nvidia_device.h"

#include <chrono>
#include <cstdio>

#include "ptx_kernel.h"

#include "gpu/constants.h"
#include "util/log.h"

namespace gpu::nvidia {

namespace {

constexpr unsigned int kCtxFlagSchedSpin = 0x01;  // CU_CTX_SCHED_SPIN

}  // namespace

NvidiaDevice::NvidiaDevice()
    : api_(nullptr),
      device_(0),
      context_(nullptr),
      module_(nullptr),
      busy_func_(nullptr),
      out_ptr_(0),
      out_bytes_(0),
      mem_load_ptr_(0),
      mem_load_bytes_(0),
      device_index_(-1) {}

NvidiaDevice::~NvidiaDevice() { Destroy(); }

void NvidiaDevice::Destroy() {
  if (api_ == nullptr) {
    return;
  }
  // Make sure operations happen against our context.
  if (context_ != nullptr) {
    api_->cuCtxSetCurrent(context_);
  }
  if (mem_load_ptr_ != 0) {
    api_->cuMemFree(mem_load_ptr_);
    mem_load_ptr_ = 0;
    mem_load_bytes_ = 0;
  }
  if (out_ptr_ != 0) {
    api_->cuMemFree(out_ptr_);
    out_ptr_ = 0;
    out_bytes_ = 0;
  }
  if (module_ != nullptr) {
    api_->cuModuleUnload(module_);
    module_ = nullptr;
  }
  if (context_ != nullptr) {
    api_->cuCtxDestroy(context_);
    context_ = nullptr;
  }
}

bool NvidiaDevice::MakeCurrent() {
  if (api_ == nullptr || context_ == nullptr) {
    return false;
  }
  const CUresult rc = api_->cuCtxSetCurrent(context_);
  if (rc != CUDA_SUCCESS) {
    LOG_WARN("cuCtxSetCurrent failed: %s", CudaErrorString(rc));
    return false;
  }
  return true;
}

bool NvidiaDevice::Init(int device_index) {
  api_ = LoadCudaApi();
  if (api_ == nullptr) {
    return false;
  }
  device_index_ = device_index;

  CUresult rc = api_->cuDeviceGet(&device_, device_index);
  if (rc != CUDA_SUCCESS) {
    LOG_WARN("cuDeviceGet(%d) failed: %s", device_index, CudaErrorString(rc));
    return false;
  }
  char name_buf[256] = {0};
  if (api_->cuDeviceGetName(name_buf, static_cast<int>(sizeof(name_buf)) - 1, device_) ==
      CUDA_SUCCESS) {
    name_buf[sizeof(name_buf) - 1] = '\0';
    name_.assign(name_buf);
  } else {
    name_.assign("NVIDIA GPU");
  }

  rc = api_->cuCtxCreate(&context_, kCtxFlagSchedSpin, device_);
  if (rc != CUDA_SUCCESS) {
    LOG_WARN("cuCtxCreate failed on device %d: %s", device_index, CudaErrorString(rc));
    return false;
  }

  rc = api_->cuModuleLoadData(&module_, kBusyKernelPtx);
  if (rc != CUDA_SUCCESS) {
    LOG_WARN("cuModuleLoadData failed: %s", CudaErrorString(rc));
    Destroy();
    return false;
  }
  rc = api_->cuModuleGetFunction(&busy_func_, module_, "busy");
  if (rc != CUDA_SUCCESS) {
    LOG_WARN("cuModuleGetFunction(busy) failed: %s", CudaErrorString(rc));
    Destroy();
    return false;
  }

  // Allocate the per-thread scratch buffer that the kernel writes into.
  const size_t threads =
      static_cast<size_t>(kGpuKernelGridSize) * static_cast<size_t>(kGpuKernelBlockSize);
  out_bytes_ = threads * sizeof(uint32_t);
  rc = api_->cuMemAlloc(&out_ptr_, out_bytes_);
  if (rc != CUDA_SUCCESS) {
    LOG_WARN("cuMemAlloc(scratch %zu) failed: %s", out_bytes_, CudaErrorString(rc));
    Destroy();
    return false;
  }
  api_->cuMemsetD8(out_ptr_, 0, out_bytes_);
  return true;
}

std::string NvidiaDevice::Name() const {
  char buf[320];
  std::snprintf(buf, sizeof(buf), "%s (#%d)", name_.c_str(), device_index_);
  return std::string(buf);
}

bool NvidiaDevice::AllocateMemory(size_t bytes) {
  if (api_ == nullptr || !MakeCurrent()) {
    return false;
  }
  if (mem_load_ptr_ != 0) {
    api_->cuMemFree(mem_load_ptr_);
    mem_load_ptr_ = 0;
    mem_load_bytes_ = 0;
  }
  const CUresult rc = api_->cuMemAlloc(&mem_load_ptr_, bytes);
  if (rc != CUDA_SUCCESS) {
    LOG_WARN("cuMemAlloc(%zu) failed: %s", bytes, CudaErrorString(rc));
    mem_load_ptr_ = 0;
    return false;
  }
  // Force physical commit.
  api_->cuMemsetD8(mem_load_ptr_, 0xA5, bytes);
  api_->cuCtxSynchronize();
  mem_load_bytes_ = bytes;
  return true;
}

void NvidiaDevice::ReleaseMemory() {
  if (api_ == nullptr || mem_load_ptr_ == 0) {
    return;
  }
  if (MakeCurrent()) {
    api_->cuMemFree(mem_load_ptr_);
  }
  mem_load_ptr_ = 0;
  mem_load_bytes_ = 0;
}

bool NvidiaDevice::RunKernel(int loop_count) {
  if (api_ == nullptr || busy_func_ == nullptr || !MakeCurrent()) {
    return false;
  }
  if (loop_count < 1) {
    loop_count = 1;
  }
  void *args[] = {&out_ptr_, &loop_count};
  CUresult rc = api_->cuLaunchKernel(busy_func_, kGpuKernelGridSize, 1, 1, kGpuKernelBlockSize, 1,
                                     1, 0, nullptr, args, nullptr);
  if (rc != CUDA_SUCCESS) {
    LOG_WARN("cuLaunchKernel failed: %s", CudaErrorString(rc));
    return false;
  }
  rc = api_->cuCtxSynchronize();
  if (rc != CUDA_SUCCESS) {
    LOG_WARN("cuCtxSynchronize failed: %s", CudaErrorString(rc));
    return false;
  }
  return true;
}

int NvidiaDevice::ProbeBaseLoopCount(int target_ms) {
  if (api_ == nullptr || busy_func_ == nullptr) {
    return 1;
  }
  // Doubling-then-bisection: start small, double until we exceed target_ms,
  // then a few iterations of bisection.
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
  LOG_DEBUG("NvidiaDevice #%d probed base_loop_count=%d (target=%dms)", device_index_, best,
            target_ms);
  return std::max(1, best);
}

}  // namespace gpu::nvidia
