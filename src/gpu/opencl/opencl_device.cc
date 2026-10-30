#include "opencl_device.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "cl_kernel.h"

#include "gpu/constants.h"
#include "gpu/contention/parse.h"
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
      queue_created_(false),
      local_size_(static_cast<std::size_t>(kGpuKernelBlockSize)),
      mem_load_bytes_(0),
      device_index_(-1) {}

OpenClDevice::~OpenClDevice() { Destroy(); }

void OpenClDevice::Destroy() {
  if (api_ == nullptr) {
    return;
  }
  ReleaseMemory();
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

namespace {

// A stable identity for the physical GPU behind `device`, or empty if the
// driver supports neither cl_khr_pci_bus_info nor cl_khr_device_uuid.
std::string DeviceIdentity(const ClApi *api, cl_device_id device) {
  cl_uint pci[4] = {};
  if (api->clGetDeviceInfo(device, CL_DEVICE_PCI_BUS_INFO_KHR, sizeof(pci), pci, nullptr) ==
      CL_SUCCESS) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "pci:%04x:%02x:%02x.%x", pci[0], pci[1], pci[2], pci[3]);
    return buf;
  }
  unsigned char uuid[16] = {};
  if (api->clGetDeviceInfo(device, CL_DEVICE_UUID_KHR, sizeof(uuid), uuid, nullptr) == CL_SUCCESS) {
    std::string id = "uuid:";
    for (unsigned char b : uuid) {
      char hex[3];
      std::snprintf(hex, sizeof(hex), "%02x", b);
      id += hex;
    }
    return id;
  }
  return {};
}

}  // namespace

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
  std::vector<std::string> seen;
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
      const std::string id = DeviceIdentity(api, d);
      if (!id.empty()) {
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) {
          LOG_DEBUG("OpenCL: skipping %s, already exposed by another platform", id.c_str());
          continue;
        }
        seen.push_back(id);
      }
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
  platform_name_ = plat_name;
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

  // Build the embedded kernel source. Decode out of the obfuscated
  // .rodata array into a stack-local buffer; the ICD copies the source
  // internally during clCreateProgramWithSource, so the plaintext is
  // wiped as soon as we leave this block.
  {
    util::obfuscate::Scoped<kBusyKernelClEncoded.size()> cl_src(kBusyKernelClEncoded,
                                                                kBusyKernelClKey);
    const char *src = cl_src.c_str();
    program_ = api_->clCreateProgramWithSource(context_, 1, &src, nullptr, &rc);
  }
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
  // Devices / compiled kernels limited below 256 work-items per group
  // (some Mali, PowerVR, VideoCore, or a register-heavy JIT) reject a
  // fixed 256 with CL_INVALID_WORK_GROUP_SIZE on every launch.
  std::size_t max_wg = 0;
  if (api_->clGetKernelWorkGroupInfo(kernel_, device_, CL_KERNEL_WORK_GROUP_SIZE, sizeof(max_wg),
                                     &max_wg, nullptr) == CL_SUCCESS &&
      max_wg > 0) {
    local_size_ = std::min(local_size_, max_wg);
  }
  LOG_DEBUG("OpenCL #%d: work-group size %zu", device_index_, local_size_);
  // Touch the device once, as the HIP backend's hipMemset does, so the
  // runtime's queue exists from here on; see Locator().
  const uint32_t zero = 0;
  rc = api_->clEnqueueFillBuffer(queue_, out_buf_, &zero, sizeof(zero), 0, out_bytes_, 0, nullptr,
                                 nullptr);
  queue_created_ = rc == CL_SUCCESS && api_->clFinish(queue_) == CL_SUCCESS;
  if (!queue_created_) {
    LOG_DEBUG("OpenCL #%d: clEnqueueFillBuffer(scratch) failed: %s (rc=%d)", device_index_,
              ClErrorString(rc), rc);
  }
  return true;
}

std::string OpenClDevice::Name() const {
  return name_ + " (#" + std::to_string(device_index_) + ")";
}

DeviceLocator OpenClDevice::Locator() const {
  DeviceLocator out;
  if (api_ == nullptr || device_ == nullptr) {
    return out;
  }
  cl_uint pci[4] = {};
  if (api_->clGetDeviceInfo(device_, CL_DEVICE_PCI_BUS_INFO_KHR, sizeof(pci), pci, nullptr) ==
      CL_SUCCESS) {
    out.pci_bus_id = contention::FormatPciBusId(pci[0], pci[1], pci[2], pci[3]);
  }
  cl_uint luid_valid = 0;  // cl_bool
  unsigned char luid[8] = {};
  if (api_->clGetDeviceInfo(device_, CL_DEVICE_LUID_VALID_KHR, sizeof(luid_valid), &luid_valid,
                            nullptr) == CL_SUCCESS &&
      luid_valid != 0 &&
      api_->clGetDeviceInfo(device_, CL_DEVICE_LUID_KHR, sizeof(luid), luid, nullptr) ==
          CL_SUCCESS) {
    // Windows LUID {DWORD LowPart; LONG HighPart}, little-endian.
    std::memcpy(&out.luid, luid, sizeof(out.luid));
  }
  // ROCm's OpenCL runs on amdkfd like HIP does.
  out.kfd_client = queue_created_ && platform_name_ == "AMD Accelerated Parallel Processing";
  return out;
}

bool OpenClDevice::AllocateMemory(std::size_t bytes) {
  if (api_ == nullptr || context_ == nullptr || queue_ == nullptr) {
    return false;
  }
  ReleaseMemory();
  cl_ulong max_alloc = 0;
  if (api_->clGetDeviceInfo(device_, CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(max_alloc), &max_alloc,
                            nullptr) != CL_SUCCESS ||
      max_alloc == 0) {
    max_alloc = bytes;
  }
  // One clCreateBuffer is capped at CL_DEVICE_MAX_MEM_ALLOC_SIZE, so a
  // large `-gm` used to fail outright; allocate it in chunks instead.
  std::vector<std::size_t> chunk_bytes;
  std::size_t remaining = bytes;
  while (remaining > 0) {
    const std::size_t chunk = static_cast<std::size_t>(std::min<cl_ulong>(remaining, max_alloc));
    cl_int rc = CL_SUCCESS;
    cl_mem buf = api_->clCreateBuffer(context_, CL_MEM_READ_WRITE, chunk, nullptr, &rc);
    if (rc != CL_SUCCESS || buf == nullptr) {
      LOG_WARN("clCreateBuffer(%zu) failed after %zu of %zu bytes: %s (rc=%d)", chunk,
               mem_load_bytes_, bytes, ClErrorString(rc), rc);
      ReleaseMemory();
      return false;
    }
    mem_load_bufs_.push_back(buf);
    chunk_bytes.push_back(chunk);
    mem_load_bytes_ += chunk;
    remaining -= chunk;
    // Force backing-store commit by filling the chunk once.
    const uint8_t pattern = 0xA5;
    rc = api_->clEnqueueFillBuffer(queue_, buf, &pattern, sizeof(pattern), 0, chunk, 0, nullptr,
                                   nullptr);
    if (rc != CL_SUCCESS) {
      // Some old ICDs may not implement clEnqueueFillBuffer well; the
      // alloc still succeeded so we don't tear it down, just note it.
      LOG_DEBUG("clEnqueueFillBuffer failed: %s (rc=%d)", ClErrorString(rc), rc);
    }
  }
  api_->clFinish(queue_);
  sweep_.Reset(chunk_bytes);
  return true;
}

void OpenClDevice::ReleaseMemory() {
  if (api_ == nullptr) {
    return;
  }
  for (cl_mem buf : mem_load_bufs_) {
    api_->clReleaseMemObject(buf);
  }
  mem_load_bufs_.clear();
  mem_load_bytes_ = 0;
  sweep_.Clear();
}

bool OpenClDevice::RunKernel(int loop_count, std::uint64_t mem_traffic_bytes) {
  if (api_ == nullptr || kernel_ == nullptr || queue_ == nullptr) {
    return false;
  }
  if (loop_count < 1) {
    loop_count = 1;
  }
  const size_t global_size = static_cast<size_t>(kGpuKernelGridSize) * local_size_;
  const SweepArgs sweep =
      sweep_.Next(loop_count, mem_traffic_bytes, static_cast<std::uint32_t>(global_size));
  // Without traffic the kernel never dereferences `mem`; any valid buffer.
  const cl_mem mem = sweep.words != 0 ? mem_load_bufs_[sweep.segment] : out_buf_;
  const cl_ulong mem_offset = sweep.offset_words;
  const cl_uint mem_words = sweep.words;
  const cl_uint mem_base = sweep.base;
  const int mem_stride = sweep.stride;
  cl_int rc = api_->clSetKernelArg(kernel_, 1, sizeof(int), &loop_count);
  if (rc == CL_SUCCESS) {
    rc = api_->clSetKernelArg(kernel_, 2, sizeof(cl_mem), &mem);
  }
  if (rc == CL_SUCCESS) {
    rc = api_->clSetKernelArg(kernel_, 3, sizeof(cl_ulong), &mem_offset);
  }
  if (rc == CL_SUCCESS) {
    rc = api_->clSetKernelArg(kernel_, 4, sizeof(cl_uint), &mem_words);
  }
  if (rc == CL_SUCCESS) {
    rc = api_->clSetKernelArg(kernel_, 5, sizeof(cl_uint), &mem_base);
  }
  if (rc == CL_SUCCESS) {
    rc = api_->clSetKernelArg(kernel_, 6, sizeof(int), &mem_stride);
  }
  if (rc != CL_SUCCESS) {
    LOG_WARN("clSetKernelArg(per-launch) failed: %s (rc=%d)", ClErrorString(rc), rc);
    return false;
  }
  rc = api_->clEnqueueNDRangeKernel(queue_, kernel_, 1, nullptr, &global_size, &local_size_, 0,
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

}  // namespace gpu::opencl
