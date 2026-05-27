#pragma once

// Minimal subset of the oneAPI Level Zero (L0) API loaded at runtime via
// dlopen / LoadLibrary. Header-self-contained: we deliberately do NOT
// include <ze_api.h> so the project builds without the oneAPI / Level Zero
// SDK installed (only the vendor-shipped loader, ze_loader.dll /
// libze_loader.so.1, has to be present at *runtime*).
//
// Sizes, enum values and struct layouts here mirror the public Level Zero
// ABI (spec v1.x). Only the handful of types/functions we actually use are
// mirrored.

#include <cstddef>
#include <cstdint>

namespace gpu::intel {

// ---- Core opaque handles -------------------------------------------------
using ze_result_t = int;
using ze_driver_handle_t = void *;
using ze_device_handle_t = void *;
using ze_context_handle_t = void *;
using ze_module_handle_t = void *;
using ze_kernel_handle_t = void *;
using ze_command_queue_handle_t = void *;
using ze_command_list_handle_t = void *;
using ze_event_pool_handle_t = void *;
using ze_event_handle_t = void *;
using ze_module_build_log_handle_t = void *;

// ---- Result codes (only the ones we check) -------------------------------
constexpr ze_result_t ZE_RESULT_SUCCESS = 0;

// ---- Init flags ----------------------------------------------------------
constexpr uint32_t ZE_INIT_FLAG_GPU_ONLY = 1u;

// ---- Structure types (subset; matches ze_structure_type_t in spec) -------
constexpr uint32_t ZE_STRUCTURE_TYPE_CONTEXT_DESC = 0x4;
constexpr uint32_t ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC = 0x5;
constexpr uint32_t ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC = 0x6;
constexpr uint32_t ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC = 0xb;
constexpr uint32_t ZE_STRUCTURE_TYPE_MODULE_DESC = 0xd;
constexpr uint32_t ZE_STRUCTURE_TYPE_KERNEL_DESC = 0x1d;
constexpr uint32_t ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES = 0x1;

// ---- Module format -------------------------------------------------------
constexpr uint32_t ZE_MODULE_FORMAT_IL_SPIRV = 0u;

// ---- Device type ---------------------------------------------------------
constexpr uint32_t ZE_DEVICE_TYPE_GPU = 1u;

// ---- Command queue config ------------------------------------------------
constexpr uint32_t ZE_COMMAND_QUEUE_MODE_DEFAULT = 0u;
constexpr uint32_t ZE_COMMAND_QUEUE_PRIORITY_NORMAL = 0u;

// ---- Structs (layout-compatible with the spec) ---------------------------
struct ze_context_desc_t {
  uint32_t stype;
  const void *pNext;
  uint32_t flags;
};

struct ze_command_queue_desc_t {
  uint32_t stype;
  const void *pNext;
  uint32_t ordinal;
  uint32_t index;
  uint32_t flags;
  uint32_t mode;
  uint32_t priority;
};

struct ze_command_list_desc_t {
  uint32_t stype;
  const void *pNext;
  uint32_t commandQueueGroupOrdinal;
  uint32_t flags;
};

struct ze_device_mem_alloc_desc_t {
  uint32_t stype;
  const void *pNext;
  uint32_t flags;
  uint32_t ordinal;
};

struct ze_module_desc_t {
  uint32_t stype;
  const void *pNext;
  uint32_t format;
  std::size_t inputSize;
  const uint8_t *pInputModule;
  const char *pBuildFlags;
  const void *pConstants;
};

struct ze_kernel_desc_t {
  uint32_t stype;
  const void *pNext;
  uint32_t flags;
  const char *pKernelName;
};

struct ze_group_count_t {
  uint32_t groupCountX;
  uint32_t groupCountY;
  uint32_t groupCountZ;
};

// ze_device_uuid_t : 16-byte opaque identifier (we only forward it around)
struct ze_device_uuid_t {
  uint8_t id[16];
};

// Layout of ze_device_properties_t -- see ze_api.h. We only read a few
// fields (name, type), but must keep the full size right so the loader
// writes within the buffer we hand it.
struct ze_device_properties_t {
  uint32_t stype;
  void *pNext;
  uint32_t type;
  uint32_t vendorId;
  uint32_t deviceId;
  uint32_t flags;
  uint32_t subdeviceId;
  uint32_t coreClockRate;
  uint64_t maxMemAllocSize;
  uint32_t maxHardwareContexts;
  uint32_t maxCommandQueuePriority;
  uint32_t numThreadsPerEU;
  uint32_t physicalEUSimdWidth;
  uint32_t numEUsPerSubslice;
  uint32_t numSubslicesPerSlice;
  uint32_t numSlices;
  uint64_t timerResolution;
  uint32_t timestampValidBits;
  uint32_t kernelTimestampValidBits;
  ze_device_uuid_t uuid;
  char name[256];  // ZE_MAX_DEVICE_NAME = 256
};

// ---- Function pointer table ---------------------------------------------
struct ZeApi {
  ze_result_t (*zeInit)(uint32_t flags);

  ze_result_t (*zeDriverGet)(uint32_t *pCount, ze_driver_handle_t *phDrivers);
  ze_result_t (*zeDeviceGet)(ze_driver_handle_t hDriver, uint32_t *pCount,
                             ze_device_handle_t *phDevices);
  ze_result_t (*zeDeviceGetProperties)(ze_device_handle_t hDevice,
                                       ze_device_properties_t *pDeviceProperties);

  ze_result_t (*zeContextCreate)(ze_driver_handle_t hDriver, const ze_context_desc_t *desc,
                                 ze_context_handle_t *phContext);
  ze_result_t (*zeContextDestroy)(ze_context_handle_t hContext);

  ze_result_t (*zeCommandQueueCreate)(ze_context_handle_t hContext, ze_device_handle_t hDevice,
                                      const ze_command_queue_desc_t *desc,
                                      ze_command_queue_handle_t *phCommandQueue);
  ze_result_t (*zeCommandQueueDestroy)(ze_command_queue_handle_t hCommandQueue);
  ze_result_t (*zeCommandQueueExecuteCommandLists)(ze_command_queue_handle_t hCommandQueue,
                                                   uint32_t numCommandLists,
                                                   ze_command_list_handle_t *phCommandLists,
                                                   void *hFence);
  ze_result_t (*zeCommandQueueSynchronize)(ze_command_queue_handle_t hCommandQueue,
                                           uint64_t timeout);

  ze_result_t (*zeCommandListCreate)(ze_context_handle_t hContext, ze_device_handle_t hDevice,
                                     const ze_command_list_desc_t *desc,
                                     ze_command_list_handle_t *phCommandList);
  ze_result_t (*zeCommandListDestroy)(ze_command_list_handle_t hCommandList);
  ze_result_t (*zeCommandListClose)(ze_command_list_handle_t hCommandList);
  ze_result_t (*zeCommandListReset)(ze_command_list_handle_t hCommandList);
  ze_result_t (*zeCommandListAppendLaunchKernel)(ze_command_list_handle_t hCommandList,
                                                 ze_kernel_handle_t hKernel,
                                                 const ze_group_count_t *pLaunchFuncArgs,
                                                 ze_event_handle_t hSignalEvent,
                                                 uint32_t numWaitEvents,
                                                 ze_event_handle_t *phWaitEvents);
  ze_result_t (*zeCommandListAppendMemoryFill)(ze_command_list_handle_t hCommandList, void *ptr,
                                               const void *pattern, std::size_t pattern_size,
                                               std::size_t size, ze_event_handle_t hSignalEvent,
                                               uint32_t numWaitEvents,
                                               ze_event_handle_t *phWaitEvents);

  ze_result_t (*zeModuleCreate)(ze_context_handle_t hContext, ze_device_handle_t hDevice,
                                const ze_module_desc_t *desc, ze_module_handle_t *phModule,
                                ze_module_build_log_handle_t *phBuildLog);
  ze_result_t (*zeModuleDestroy)(ze_module_handle_t hModule);
  ze_result_t (*zeModuleBuildLogDestroy)(ze_module_build_log_handle_t hModuleBuildLog);
  ze_result_t (*zeModuleBuildLogGetString)(ze_module_build_log_handle_t hModuleBuildLog,
                                           std::size_t *pSize, char *pBuildLog);

  ze_result_t (*zeKernelCreate)(ze_module_handle_t hModule, const ze_kernel_desc_t *desc,
                                ze_kernel_handle_t *phKernel);
  ze_result_t (*zeKernelDestroy)(ze_kernel_handle_t hKernel);
  ze_result_t (*zeKernelSetGroupSize)(ze_kernel_handle_t hKernel, uint32_t groupSizeX,
                                      uint32_t groupSizeY, uint32_t groupSizeZ);
  ze_result_t (*zeKernelSetArgumentValue)(ze_kernel_handle_t hKernel, uint32_t argIndex,
                                          std::size_t argSize, const void *pArgValue);

  ze_result_t (*zeMemAllocDevice)(ze_context_handle_t hContext,
                                  const ze_device_mem_alloc_desc_t *device_desc,
                                  std::size_t size, std::size_t alignment,
                                  ze_device_handle_t hDevice, void **pptr);
  ze_result_t (*zeMemFree)(ze_context_handle_t hContext, void *ptr);
};

// Returns a singleton API table; first call dlopens ze_loader and
// resolves the symbols. Returns nullptr if Level Zero isn't usable on
// this host (loader missing, zeInit failed, no GPU).
const ZeApi *LoadZeApi();

// Convenience: stringify a result code.
const char *ZeResultString(ze_result_t err);

}  // namespace gpu::intel
