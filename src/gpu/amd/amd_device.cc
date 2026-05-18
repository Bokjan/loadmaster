#include "amd_device.h"

#include <chrono>
#include <cstdio>

#include "hip_kernel.h"

#include "core/constants.h"
#include "util/log.h"

namespace gpu::amd {

AmdDevice::AmdDevice()
    : api_(nullptr),
      device_(0),
      context_(nullptr),
      module_(nullptr),
      busy_func_(nullptr),
      out_ptr_(nullptr),
      out_bytes_(0),
      mem_load_ptr_(nullptr),
      mem_load_bytes_(0),
      device_index_(-1) {}

AmdDevice::~AmdDevice() { Destroy(); }

void AmdDevice::Destroy() {
  if (api_ == nullptr) {
    return;
  }
  if (context_ != nullptr) {
    api_->hipCtxSetCurrent(context_);
  }
  if (mem_load_ptr_ != nullptr) {
    api_->hipFree(mem_load_ptr_);
    mem_load_ptr_ = nullptr;
    mem_load_bytes_ = 0;
  }
  if (out_ptr_ != nullptr) {
    api_->hipFree(out_ptr_);
    out_ptr_ = nullptr;
    out_bytes_ = 0;
  }
  if (module_ != nullptr) {
    api_->hipModuleUnload(module_);
    module_ = nullptr;
  }
  if (context_ != nullptr) {
    api_->hipCtxDestroy(context_);
    context_ = nullptr;
  }
}

bool AmdDevice::MakeCurrent() {
  if (api_ == nullptr || context_ == nullptr) {
    return false;
  }
  const hipError_t rc = api_->hipCtxSetCurrent(context_);
  if (rc != hipSuccess) {
    LOG_WARN("hipCtxSetCurrent failed: %s", HipErrorString(rc));
    return false;
  }
  return true;
}

bool AmdDevice::CompileKernel(std::vector<char> &code_out) {
  hiprtcProgram prog = nullptr;
  hiprtcResult rc = api_->hiprtcCreateProgram(&prog, kBusyKernelHipSrc, "busy.hip", 0, nullptr,
                                              nullptr);
  if (rc != HIPRTC_SUCCESS) {
    LOG_WARN("hiprtcCreateProgram failed: %s", HiprtcErrorString(rc));
    return false;
  }

  // No special options: hipRTC defaults will target the host's GPU.
  rc = api_->hiprtcCompileProgram(prog, 0, nullptr);
  if (rc != HIPRTC_SUCCESS) {
    size_t log_size = 0;
    api_->hiprtcGetProgramLogSize(prog, &log_size);
    if (log_size > 1) {
      std::vector<char> log(log_size);
      api_->hiprtcGetProgramLog(prog, log.data());
      LOG_WARN("hiprtcCompileProgram failed: %s\n%s", HiprtcErrorString(rc), log.data());
    } else {
      LOG_WARN("hiprtcCompileProgram failed: %s", HiprtcErrorString(rc));
    }
    api_->hiprtcDestroyProgram(&prog);
    return false;
  }

  size_t code_size = 0;
  rc = api_->hiprtcGetCodeSize(prog, &code_size);
  if (rc != HIPRTC_SUCCESS || code_size == 0) {
    LOG_WARN("hiprtcGetCodeSize failed: %s (size=%zu)", HiprtcErrorString(rc), code_size);
    api_->hiprtcDestroyProgram(&prog);
    return false;
  }
  code_out.assign(code_size, 0);
  rc = api_->hiprtcGetCode(prog, code_out.data());
  api_->hiprtcDestroyProgram(&prog);
  if (rc != HIPRTC_SUCCESS) {
    LOG_WARN("hiprtcGetCode failed: %s", HiprtcErrorString(rc));
    return false;
  }
  return true;
}

bool AmdDevice::Init(int device_index) {
  api_ = LoadHipApi();
  if (api_ == nullptr) {
    return false;
  }
  device_index_ = device_index;

  hipError_t rc = api_->hipDeviceGet(&device_, device_index);
  if (rc != hipSuccess) {
    LOG_WARN("hipDeviceGet(%d) failed: %s", device_index, HipErrorString(rc));
    return false;
  }
  char name_buf[256] = {0};
  if (api_->hipDeviceGetName(name_buf, static_cast<int>(sizeof(name_buf)) - 1, device_) ==
      hipSuccess) {
    name_buf[sizeof(name_buf) - 1] = '\0';
    name_.assign(name_buf);
  } else {
    name_.assign("AMD GPU");
  }

  rc = api_->hipCtxCreate(&context_, 0, device_);
  if (rc != hipSuccess) {
    LOG_WARN("hipCtxCreate failed on device %d: %s", device_index, HipErrorString(rc));
    return false;
  }

  std::vector<char> code;
  if (!CompileKernel(code)) {
    Destroy();
    return false;
  }
  rc = api_->hipModuleLoadData(&module_, code.data());
  if (rc != hipSuccess) {
    LOG_WARN("hipModuleLoadData failed: %s", HipErrorString(rc));
    Destroy();
    return false;
  }
  rc = api_->hipModuleGetFunction(&busy_func_, module_, "busy");
  if (rc != hipSuccess) {
    LOG_WARN("hipModuleGetFunction(busy) failed: %s", HipErrorString(rc));
    Destroy();
    return false;
  }

  const size_t threads =
      static_cast<size_t>(kGpuKernelGridSize) * static_cast<size_t>(kGpuKernelBlockSize);
  out_bytes_ = threads * sizeof(uint32_t);
  rc = api_->hipMalloc(&out_ptr_, out_bytes_);
  if (rc != hipSuccess) {
    LOG_WARN("hipMalloc(scratch %zu) failed: %s", out_bytes_, HipErrorString(rc));
    Destroy();
    return false;
  }
  api_->hipMemset(out_ptr_, 0, out_bytes_);
  return true;
}

std::string AmdDevice::Name() const {
  char buf[320];
  std::snprintf(buf, sizeof(buf), "%s (#%d)", name_.c_str(), device_index_);
  return std::string(buf);
}

bool AmdDevice::AllocateMemory(size_t bytes) {
  if (api_ == nullptr || !MakeCurrent()) {
    return false;
  }
  if (mem_load_ptr_ != nullptr) {
    api_->hipFree(mem_load_ptr_);
    mem_load_ptr_ = nullptr;
    mem_load_bytes_ = 0;
  }
  const hipError_t rc = api_->hipMalloc(&mem_load_ptr_, bytes);
  if (rc != hipSuccess) {
    LOG_WARN("hipMalloc(%zu) failed: %s", bytes, HipErrorString(rc));
    mem_load_ptr_ = nullptr;
    return false;
  }
  api_->hipMemset(mem_load_ptr_, 0xA5, bytes);
  api_->hipCtxSynchronize();
  mem_load_bytes_ = bytes;
  return true;
}

void AmdDevice::ReleaseMemory() {
  if (api_ == nullptr || mem_load_ptr_ == nullptr) {
    return;
  }
  if (MakeCurrent()) {
    api_->hipFree(mem_load_ptr_);
  }
  mem_load_ptr_ = nullptr;
  mem_load_bytes_ = 0;
}

bool AmdDevice::RunKernel(int loop_count) {
  if (api_ == nullptr || busy_func_ == nullptr || !MakeCurrent()) {
    return false;
  }
  if (loop_count < 1) {
    loop_count = 1;
  }
  void *args[] = {&out_ptr_, &loop_count};
  hipError_t rc = api_->hipModuleLaunchKernel(busy_func_, kGpuKernelGridSize, 1, 1,
                                              kGpuKernelBlockSize, 1, 1, 0, nullptr, args, nullptr);
  if (rc != hipSuccess) {
    LOG_WARN("hipModuleLaunchKernel failed: %s", HipErrorString(rc));
    return false;
  }
  rc = api_->hipCtxSynchronize();
  if (rc != hipSuccess) {
    LOG_WARN("hipCtxSynchronize failed: %s", HipErrorString(rc));
    return false;
  }
  return true;
}

int AmdDevice::ProbeBaseLoopCount(int target_ms) {
  if (api_ == nullptr || busy_func_ == nullptr) {
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
  LOG_DEBUG("AmdDevice #%d probed base_loop_count=%d (target=%dms)", device_index_, best,
            target_ms);
  return std::max(1, best);
}

}  // namespace gpu::amd
