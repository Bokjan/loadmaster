#pragma once

// Pure string helpers for the contention sources. Kept free of OS calls so
// the Windows / macOS formats are unit-tested on every host.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace gpu::contention {

// "dddd:bb:dd.f" in lower-case hex.
std::string FormatPciBusId(uint32_t domain, uint32_t bus, uint32_t device, uint32_t function);

// Normalise the PCI address spellings the vendor APIs and sysfs use --
// "0000:03:00.0" (sysfs, OpenCL), "00000000:03:00.0" (NVML), "0000:0A:00.0"
// (CUDA, upper case), "03:00.0" (domain 0) -- to FormatPciBusId's form.
// Returns empty for anything else (e.g. a platform device name on an ARM
// SoC, "fec00000.v3d").
std::string CanonicalPciBusId(std::string_view text);

// amdkfd topology: a node's `properties` file holds one "key value" pair per
// line. Returns the value of `key`, or nullopt.
std::optional<uint64_t> KfdProperty(std::string_view properties, std::string_view key);

// amdkfd topology `location_id` packs the PCI bus, device and function as
// bus << 8 | device << 3 | function (the kernel's pci_dev_id()).
std::string KfdPciBusId(uint32_t domain, uint32_t location_id);

// Linux /proc/<pid>/maps: does the process have a compute runtime (HIP /
// ROCr, an OpenCL ICD, Level Zero) for a GPU driven by DRM driver
// `drm_driver` ("i915", "amdgpu", "v3d", ...) mapped? An open DRM node
// alone doesn't tell compute from graphics. Only runtimes that can drive
// that driver's GPUs count: a process on the Intel node with libcuda
// mapped (nvidia-vaapi-driver on a hybrid laptop) isn't computing on it.
bool MapsComputeRuntime(std::string_view maps, std::string_view drm_driver);

// Linux DRM client usage stats, /proc/<pid>/fdinfo/<fd> of a DRM node
// (Documentation/gpu/drm-usage-stats.rst). Accumulated since the client
// opened the node.
struct DrmFdinfo {
  // drm-client-id: fds dup()ed from one open share it. 0 if absent.
  uint64_t client_id = 0;
  // Sum of drm-engine-<engine> busy time (amdgpu, i915, v3d, panfrost, ...).
  bool has_engine_ns = false;
  uint64_t engine_ns = 0;
  // xe: engine class -> (drm-cycles-<class>, drm-total-cycles-<class>),
  // classes that have both only.
  std::map<std::string, std::pair<uint64_t, uint64_t>, std::less<>> cycles;
};
// nullopt when `text` isn't a DRM fdinfo (no drm-driver line).
std::optional<DrmFdinfo> ParseDrmFdinfo(std::string_view text);

// Busy fraction of a DRM client between two readings `wall_ns` apart (1.0
// = one engine busy all the time; several busy engines add up). nullopt
// when the driver reports no usage stats, or `wall_ns` is not positive.
std::optional<double> DrmBusyFraction(const DrmFdinfo &before, const DrmFdinfo &after,
                                      double wall_ns);

// One instance of the Windows "GPU Engine" performance counter set, e.g.
// "pid_1234_luid_0x00000000_0x0000D1B2_phys_0_eng_3_engtype_Compute_0".
struct GpuEngineInstance {
  int pid = 0;
  uint64_t luid = 0;             // (HighPart << 32) | LowPart
  std::string_view engine_type;  // points into the parsed name
};
std::optional<GpuEngineInstance> ParseGpuEngineInstance(std::string_view name);

// Is a "GPU Engine" engine type a compute one ("Compute", "Compute_0",
// "Cuda", ...) rather than 3D / copy / video?
bool IsComputeEngineType(std::string_view engine_type);

// macOS IOUserClientCreator, "pid 123, WindowServer": pid and process name.
struct UserClientCreator {
  int pid = 0;
  std::string_view name;
};
std::optional<UserClientCreator> ParseUserClientCreator(std::string_view text);

}  // namespace gpu::contention
