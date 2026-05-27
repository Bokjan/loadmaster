#include "intel_device.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include "spirv_kernel.h"

#include "gpu/constants.h"
#include "util/log.h"

namespace gpu::intel {

namespace {

// Enumerate every (driver, device) GPU pair across all installed L0
// drivers, in stable order. The factory passes us a single flat
// index, so we flatten here.
struct DriverDevicePair {
  ze_driver_handle_t driver;
  ze_device_handle_t device;
};

std::vector<DriverDevicePair> CollectGpuDevices(const ZeApi *api) {
  std::vector<DriverDevicePair> out;
  uint32_t driver_count = 0;
  if (api->zeDriverGet(&driver_count, nullptr) != ZE_RESULT_SUCCESS || driver_count == 0) {
    return out;
  }
  std::vector<ze_driver_handle_t> drivers(driver_count);
  if (api->zeDriverGet(&driver_count, drivers.data()) != ZE_RESULT_SUCCESS) {
    return out;
  }
  for (auto drv : drivers) {
    uint32_t dev_count = 0;
    if (api->zeDeviceGet(drv, &dev_count, nullptr) != ZE_RESULT_SUCCESS || dev_count == 0) {
      continue;
    }
    std::vector<ze_device_handle_t> devs(dev_count);
    if (api->zeDeviceGet(drv, &dev_count, devs.data()) != ZE_RESULT_SUCCESS) {
      continue;
    }
    for (auto d : devs) {
      ze_device_properties_t props{};
      props.stype = ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES;
      if (api->zeDeviceGetProperties(d, &props) != ZE_RESULT_SUCCESS) {
        continue;
      }
      if (props.type != ZE_DEVICE_TYPE_GPU) {
        continue;
      }
      out.push_back({drv, d});
    }
  }
  return out;
}

}  // namespace

IntelDevice::IntelDevice()
    : api_(nullptr),
      driver_(nullptr),
      device_(nullptr),
      context_(nullptr),
      queue_(nullptr),
      cmd_list_(nullptr),
      module_(nullptr),
      kernel_(nullptr),
      out_ptr_(nullptr),
      out_bytes_(0),
      mem_load_ptr_(nullptr),
      mem_load_bytes_(0),
      device_index_(-1) {}

IntelDevice::~IntelDevice() { Destroy(); }

void IntelDevice::Destroy() {
  if (api_ == nullptr) {
    return;
  }
  if (mem_load_ptr_ != nullptr && context_ != nullptr) {
    api_->zeMemFree(context_, mem_load_ptr_);
    mem_load_ptr_ = nullptr;
    mem_load_bytes_ = 0;
  }
  if (out_ptr_ != nullptr && context_ != nullptr) {
    api_->zeMemFree(context_, out_ptr_);
    out_ptr_ = nullptr;
    out_bytes_ = 0;
  }
  if (kernel_ != nullptr) {
    api_->zeKernelDestroy(kernel_);
    kernel_ = nullptr;
  }
  if (module_ != nullptr) {
    api_->zeModuleDestroy(module_);
    module_ = nullptr;
  }
  if (cmd_list_ != nullptr) {
    api_->zeCommandListDestroy(cmd_list_);
    cmd_list_ = nullptr;
  }
  if (queue_ != nullptr) {
    api_->zeCommandQueueDestroy(queue_);
    queue_ = nullptr;
  }
  if (context_ != nullptr) {
    api_->zeContextDestroy(context_);
    context_ = nullptr;
  }
}

bool IntelDevice::Init(int device_index) {
  if (!kBusyKernelSpirvAvailable) {
    LOG_INFO(
        "Intel GPU backend disabled: no SPIR-V kernel embedded in this build. "
        "Generate src/gpu/intel/busy_kernel.spv (see busy_kernel.cl) and rebuild "
        "to enable the Level Zero backend.");
    return false;
  }
  api_ = LoadZeApi();
  if (api_ == nullptr) {
    return false;
  }
  device_index_ = device_index;

  // Resolve the flat index into a concrete (driver, device) pair.
  const auto pairs = CollectGpuDevices(api_);
  if (pairs.empty()) {
    LOG_INFO("Intel: no Level Zero GPU devices reported");
    return false;
  }
  if (device_index < 0 || static_cast<size_t>(device_index) >= pairs.size()) {
    LOG_WARN("Intel: device index %d out of range (have %zu)", device_index, pairs.size());
    return false;
  }
  driver_ = pairs[device_index].driver;
  device_ = pairs[device_index].device;

  ze_device_properties_t props{};
  props.stype = ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES;
  if (api_->zeDeviceGetProperties(device_, &props) == ZE_RESULT_SUCCESS) {
    // `name` is NUL-terminated per spec; defensively clamp.
    props.name[sizeof(props.name) - 1] = '\0';
    name_.assign(props.name);
  } else {
    name_.assign("Intel GPU");
  }

  ze_context_desc_t ctx_desc{};
  ctx_desc.stype = ZE_STRUCTURE_TYPE_CONTEXT_DESC;
  ze_result_t rc = api_->zeContextCreate(driver_, &ctx_desc, &context_);
  if (rc != ZE_RESULT_SUCCESS) {
    LOG_WARN("zeContextCreate failed: %s (rc=0x%x)", ZeResultString(rc), rc);
    return false;
  }

  ze_command_queue_desc_t q_desc{};
  q_desc.stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC;
  q_desc.ordinal = 0;
  q_desc.index = 0;
  q_desc.mode = ZE_COMMAND_QUEUE_MODE_DEFAULT;
  q_desc.priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL;
  rc = api_->zeCommandQueueCreate(context_, device_, &q_desc, &queue_);
  if (rc != ZE_RESULT_SUCCESS) {
    LOG_WARN("zeCommandQueueCreate failed: %s (rc=0x%x)", ZeResultString(rc), rc);
    Destroy();
    return false;
  }

  ze_command_list_desc_t cl_desc{};
  cl_desc.stype = ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC;
  cl_desc.commandQueueGroupOrdinal = 0;
  rc = api_->zeCommandListCreate(context_, device_, &cl_desc, &cmd_list_);
  if (rc != ZE_RESULT_SUCCESS) {
    LOG_WARN("zeCommandListCreate failed: %s (rc=0x%x)", ZeResultString(rc), rc);
    Destroy();
    return false;
  }

  // Build the SPIR-V module.
  ze_module_desc_t mod_desc{};
  mod_desc.stype = ZE_STRUCTURE_TYPE_MODULE_DESC;
  mod_desc.format = ZE_MODULE_FORMAT_IL_SPIRV;
  mod_desc.inputSize = kBusyKernelSpirvSize;
  mod_desc.pInputModule = kBusyKernelSpirv;
  ze_module_build_log_handle_t build_log = nullptr;
  rc = api_->zeModuleCreate(context_, device_, &mod_desc, &module_, &build_log);
  if (rc != ZE_RESULT_SUCCESS) {
    if (build_log != nullptr) {
      size_t log_size = 0;
      if (api_->zeModuleBuildLogGetString(build_log, &log_size, nullptr) == ZE_RESULT_SUCCESS &&
          log_size > 1) {
        std::vector<char> log(log_size);
        api_->zeModuleBuildLogGetString(build_log, &log_size, log.data());
        LOG_WARN("zeModuleCreate failed: %s\n%s", ZeResultString(rc), log.data());
      } else {
        LOG_WARN("zeModuleCreate failed: %s (rc=0x%x)", ZeResultString(rc), rc);
      }
      api_->zeModuleBuildLogDestroy(build_log);
    } else {
      LOG_WARN("zeModuleCreate failed: %s (rc=0x%x)", ZeResultString(rc), rc);
    }
    Destroy();
    return false;
  }
  if (build_log != nullptr) {
    api_->zeModuleBuildLogDestroy(build_log);
  }

  ze_kernel_desc_t kdesc{};
  kdesc.stype = ZE_STRUCTURE_TYPE_KERNEL_DESC;
  kdesc.pKernelName = "busy";
  rc = api_->zeKernelCreate(module_, &kdesc, &kernel_);
  if (rc != ZE_RESULT_SUCCESS) {
    LOG_WARN("zeKernelCreate(busy) failed: %s (rc=0x%x)", ZeResultString(rc), rc);
    Destroy();
    return false;
  }
  // Match the cross-vendor grid/block configuration: grid X = kGpuKernelGridSize,
  // local size X = kGpuKernelBlockSize.
  rc = api_->zeKernelSetGroupSize(kernel_, static_cast<uint32_t>(kGpuKernelBlockSize), 1, 1);
  if (rc != ZE_RESULT_SUCCESS) {
    LOG_WARN("zeKernelSetGroupSize failed: %s (rc=0x%x)", ZeResultString(rc), rc);
    Destroy();
    return false;
  }

  // Scratch buffer the kernel writes into (1 uint per work-item).
  const size_t threads =
      static_cast<size_t>(kGpuKernelGridSize) * static_cast<size_t>(kGpuKernelBlockSize);
  out_bytes_ = threads * sizeof(uint32_t);
  ze_device_mem_alloc_desc_t mem_desc{};
  mem_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
  rc = api_->zeMemAllocDevice(context_, &mem_desc, out_bytes_, 64, device_, &out_ptr_);
  if (rc != ZE_RESULT_SUCCESS) {
    LOG_WARN("zeMemAllocDevice(scratch %zu) failed: %s (rc=0x%x)", out_bytes_,
             ZeResultString(rc), rc);
    Destroy();
    return false;
  }

  // Bind arg 0 = out pointer (the loop_count is rebound every launch).
  rc = api_->zeKernelSetArgumentValue(kernel_, 0, sizeof(out_ptr_), &out_ptr_);
  if (rc != ZE_RESULT_SUCCESS) {
    LOG_WARN("zeKernelSetArgumentValue(out) failed: %s (rc=0x%x)", ZeResultString(rc), rc);
    Destroy();
    return false;
  }
  return true;
}

std::string IntelDevice::Name() const {
  char buf[320];
  std::snprintf(buf, sizeof(buf), "%s (#%d)", name_.c_str(), device_index_);
  return std::string(buf);
}

bool IntelDevice::AllocateMemory(std::size_t bytes) {
  if (api_ == nullptr || context_ == nullptr) {
    return false;
  }
  if (mem_load_ptr_ != nullptr) {
    api_->zeMemFree(context_, mem_load_ptr_);
    mem_load_ptr_ = nullptr;
    mem_load_bytes_ = 0;
  }
  ze_device_mem_alloc_desc_t mem_desc{};
  mem_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
  ze_result_t rc =
      api_->zeMemAllocDevice(context_, &mem_desc, bytes, 64, device_, &mem_load_ptr_);
  if (rc != ZE_RESULT_SUCCESS) {
    LOG_WARN("zeMemAllocDevice(%zu) failed: %s (rc=0x%x)", bytes, ZeResultString(rc), rc);
    mem_load_ptr_ = nullptr;
    return false;
  }

  // Force a real backing-store commit by filling once via a transient
  // command list. Reusing the persistent cmd_list_ would force us to
  // reset its recorded launch state, which is wasteful here.
  ze_command_list_desc_t cl_desc{};
  cl_desc.stype = ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC;
  cl_desc.commandQueueGroupOrdinal = 0;
  ze_command_list_handle_t tmp = nullptr;
  if (api_->zeCommandListCreate(context_, device_, &cl_desc, &tmp) == ZE_RESULT_SUCCESS) {
    const uint8_t pattern = 0xA5;
    if (api_->zeCommandListAppendMemoryFill(tmp, mem_load_ptr_, &pattern, sizeof(pattern), bytes,
                                            nullptr, 0, nullptr) == ZE_RESULT_SUCCESS &&
        api_->zeCommandListClose(tmp) == ZE_RESULT_SUCCESS) {
      api_->zeCommandQueueExecuteCommandLists(queue_, 1, &tmp, nullptr);
      api_->zeCommandQueueSynchronize(queue_, UINT64_MAX);
    }
    api_->zeCommandListDestroy(tmp);
  }
  mem_load_bytes_ = bytes;
  return true;
}

void IntelDevice::ReleaseMemory() {
  if (api_ == nullptr || context_ == nullptr || mem_load_ptr_ == nullptr) {
    return;
  }
  api_->zeMemFree(context_, mem_load_ptr_);
  mem_load_ptr_ = nullptr;
  mem_load_bytes_ = 0;
}

bool IntelDevice::RunKernel(int loop_count) {
  if (api_ == nullptr || kernel_ == nullptr || cmd_list_ == nullptr || queue_ == nullptr) {
    return false;
  }
  if (loop_count < 1) {
    loop_count = 1;
  }
  // Re-bind the per-launch scalar arg (out pointer was bound once at Init).
  ze_result_t rc = api_->zeKernelSetArgumentValue(kernel_, 1, sizeof(loop_count), &loop_count);
  if (rc != ZE_RESULT_SUCCESS) {
    LOG_WARN("zeKernelSetArgumentValue(loop) failed: %s (rc=0x%x)", ZeResultString(rc), rc);
    return false;
  }

  // Build a one-shot command list each tick. Cheaper alternatives exist
  // (immediate command list, recorded+reset list) but the launch cadence
  // is only 10 Hz and clarity wins.
  rc = api_->zeCommandListReset(cmd_list_);
  if (rc != ZE_RESULT_SUCCESS) {
    LOG_WARN("zeCommandListReset failed: %s (rc=0x%x)", ZeResultString(rc), rc);
    return false;
  }
  ze_group_count_t grid{static_cast<uint32_t>(kGpuKernelGridSize), 1, 1};
  rc = api_->zeCommandListAppendLaunchKernel(cmd_list_, kernel_, &grid, nullptr, 0, nullptr);
  if (rc != ZE_RESULT_SUCCESS) {
    LOG_WARN("zeCommandListAppendLaunchKernel failed: %s (rc=0x%x)", ZeResultString(rc), rc);
    return false;
  }
  rc = api_->zeCommandListClose(cmd_list_);
  if (rc != ZE_RESULT_SUCCESS) {
    LOG_WARN("zeCommandListClose failed: %s (rc=0x%x)", ZeResultString(rc), rc);
    return false;
  }
  rc = api_->zeCommandQueueExecuteCommandLists(queue_, 1, &cmd_list_, nullptr);
  if (rc != ZE_RESULT_SUCCESS) {
    LOG_WARN("zeCommandQueueExecuteCommandLists failed: %s (rc=0x%x)", ZeResultString(rc), rc);
    return false;
  }
  rc = api_->zeCommandQueueSynchronize(queue_, UINT64_MAX);
  if (rc != ZE_RESULT_SUCCESS) {
    LOG_WARN("zeCommandQueueSynchronize failed: %s (rc=0x%x)", ZeResultString(rc), rc);
    return false;
  }
  return true;
}

int IntelDevice::ProbeBaseLoopCount(int target_ms) {
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
  LOG_DEBUG("IntelDevice #%d probed base_loop_count=%d (target=%dms)", device_index_, best,
            target_ms);
  return std::max(1, best);
}

}  // namespace gpu::intel
