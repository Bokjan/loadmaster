#include "nvidia_device.h"

#include <cstdio>

#include "ptx_kernel.h"

#include "gpu/constants.h"
#include "util/log.h"

namespace gpu::nvidia {

namespace {

// CU_CTX_SCHED_BLOCKING_SYNC: cuCtxSynchronize() sleeps on an OS primitive
// until the kernel finishes. The previous CU_CTX_SCHED_SPIN busy-polled,
// burning a full host core per GPU for the whole kernel -- on top of (and
// invisible to) the CPU module's own load target.
constexpr unsigned int kCtxFlagSchedBlockingSync = 0x04;

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

  rc = api_->cuCtxCreate(&context_, kCtxFlagSchedBlockingSync, device_);
  if (rc != CUDA_SUCCESS) {
    LOG_WARN("cuCtxCreate failed on device %d: %s", device_index, CudaErrorString(rc));
    return false;
  }

  // Decode the obfuscated PTX into a stack-local buffer, hand it to the
  // driver, then drop out of scope so the plaintext is wiped. The
  // driver itself keeps an internal copy in `module_`, but our
  // .rodata never sees the unmasked source.
  {
    util::obfuscate::Scoped<kBusyKernelPtxEncoded.size()> ptx(kBusyKernelPtxEncoded,
                                                              kBusyKernelPtxKey);
    rc = api_->cuModuleLoadData(&module_, ptx.c_str());
  }
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
  // Force physical commit by faulting every page. Check the sync: a failure
  // here usually means a real device error, and silently holding
  // uncommitted VRAM is worse than reporting the allocation as failed.
  const CUresult set_rc = api_->cuMemsetD8(mem_load_ptr_, 0xA5, bytes);
  if (set_rc != CUDA_SUCCESS) {
    LOG_WARN("cuMemsetD8(%zu) failed: %s", bytes, CudaErrorString(set_rc));
  }
  const CUresult sync_rc = api_->cuCtxSynchronize();
  if (sync_rc != CUDA_SUCCESS) {
    LOG_WARN("cuCtxSynchronize after memset failed: %s -- releasing %zu bytes",
             CudaErrorString(sync_rc), bytes);
    api_->cuMemFree(mem_load_ptr_);
    mem_load_ptr_ = 0;
    return false;
  }
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

}  // namespace gpu::nvidia
