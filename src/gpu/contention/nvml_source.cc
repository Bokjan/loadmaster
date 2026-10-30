#include "nvml_source.h"

#include <cstring>

#include "core/platform.h"
#include "util/log.h"

namespace gpu::contention {

namespace {

constexpr int kNvmlSuccess = 0;
constexpr int kNvmlErrorInvalidArgument = 2;
constexpr int kNvmlErrorNotFound = 6;
constexpr int kNvmlErrorInsufficientSize = 7;
constexpr int kNvmlDriverWddm = 0;

// nvmlProcessInfo_v2_t, filled by the _v2 and _v3 entry points (R450+):
// { pid; usedGpuMemory; gpuInstanceId; computeInstanceId }.
struct ProcessInfoV2 {
  unsigned int pid;
  unsigned long long used_gpu_memory;
  unsigned int gpu_instance_id;
  unsigned int compute_instance_id;
};
// nvmlProcessInfo_v1_t, filled by the unversioned entry point (older
// drivers): { pid; usedGpuMemory }.
struct ProcessInfoV1 {
  unsigned int pid;
  unsigned long long used_gpu_memory;
};

}  // namespace

NvmlSource::NvmlSource(int self_pid)
    : self_pid_(self_pid),
      ready_(false),
      lib_(nullptr),
      init_(nullptr),
      shutdown_(nullptr),
      get_handle_by_pci_bus_id_(nullptr),
      get_compute_processes_(nullptr),
      process_info_size_(0),
      get_driver_model_(nullptr) {
  // Ships with the NVIDIA driver: libnvidia-ml.so.1 on Linux, nvml.dll in
  // System32 on Windows (R450 and later).
  static const char *const kCandidates[] = {
#if IS_WINDOWS
      "nvml.dll",
#else
      "libnvidia-ml.so.1",
      "libnvidia-ml.so",
#endif
      nullptr,
  };
  lib_ = util::DlopenAny(kCandidates);
  if (lib_ == nullptr) {
    LOG_DEBUG("NVML not available: %s", util::Dlerror());
    return;
  }
  bool ok = util::DlsymInto(lib_, "nvmlInit_v2", init_);
  ok = ok && util::DlsymInto(lib_, "nvmlShutdown", shutdown_);
  ok = ok && util::DlsymInto(lib_, "nvmlDeviceGetHandleByPciBusId_v2", get_handle_by_pci_bus_id_);
  if (util::DlsymInto(lib_, "nvmlDeviceGetComputeRunningProcesses_v3", get_compute_processes_) ||
      util::DlsymInto(lib_, "nvmlDeviceGetComputeRunningProcesses_v2", get_compute_processes_)) {
    process_info_size_ = sizeof(ProcessInfoV2);
  } else if (util::DlsymInto(lib_, "nvmlDeviceGetComputeRunningProcesses",
                             get_compute_processes_)) {
    process_info_size_ = sizeof(ProcessInfoV1);
  } else {
    ok = false;
  }
#if IS_WINDOWS
  if (!util::DlsymInto(lib_, "nvmlDeviceGetDriverModel_v2", get_driver_model_) &&
      !util::DlsymInto(lib_, "nvmlDeviceGetDriverModel", get_driver_model_)) {
    ok = false;  // can't tell WDDM devices apart, and those mislead
  }
#endif
  if (!ok) {
    LOG_WARN("NVML: missing entry points, not using it");
    util::Dlclose(lib_);
    lib_ = nullptr;
    return;
  }
  const Return rc = init_();
  if (rc != kNvmlSuccess) {
    LOG_INFO("nvmlInit failed (rc=%d), not using NVML", rc);
    util::Dlclose(lib_);
    lib_ = nullptr;
    return;
  }
  ready_ = true;
}

NvmlSource::~NvmlSource() {
  if (ready_) {
    shutdown_();
  }
  util::Dlclose(lib_);
}

bool NvmlSource::ComputePids(DeviceHandle device, std::vector<int> &pids) {
  // Two tries: the list can grow between the size query and the fetch.
  unsigned int count = 16;
  std::vector<unsigned char> buf;
  for (int attempt = 0; attempt < 2; ++attempt) {
    count += 8;
    buf.assign(count * process_info_size_, 0);
    const Return rc = get_compute_processes_(device, &count, buf.data());
    if (rc == kNvmlErrorInsufficientSize) {
      continue;
    }
    if (rc != kNvmlSuccess) {
      LOG_DEBUG("nvmlDeviceGetComputeRunningProcesses failed (rc=%d)", rc);
      return false;
    }
    pids.clear();
    for (unsigned int i = 0; i < count; ++i) {
      // pid is the first member in every version.
      unsigned int pid = 0;
      std::memcpy(&pid, buf.data() + i * process_info_size_, sizeof(pid));
      pids.push_back(static_cast<int>(pid));
    }
    return true;
  }
  return false;
}

void NvmlSource::Sample(const std::vector<DeviceLocator> &devices, std::vector<Observation> &out) {
  if (!ready_) {
    return;
  }
  std::vector<int> pids;
  for (size_t i = 0; i < devices.size(); ++i) {
    const std::string &pci = devices[i].pci_bus_id;
    if (pci.empty()) {
      continue;
    }
    auto it = handles_.find(pci);
    if (it == handles_.end()) {
      DeviceHandle handle = nullptr;
      const Return rc = get_handle_by_pci_bus_id_(pci.c_str(), &handle);
      if (rc == kNvmlErrorNotFound || rc == kNvmlErrorInvalidArgument) {
        handle = nullptr;  // not an NVIDIA device: remember, don't ask again
      } else if (rc != kNvmlSuccess) {
        // Anything else may be transient (driver busy, ...): retry next poll.
        LOG_DEBUG("nvmlDeviceGetHandleByPciBusId(%s) failed (rc=%d)", pci.c_str(), rc);
        continue;
      }
      if (handle != nullptr && get_driver_model_ != nullptr) {
        int current = kNvmlDriverWddm;
        int pending = kNvmlDriverWddm;
        if (get_driver_model_(handle, &current, &pending) != kNvmlSuccess ||
            current == kNvmlDriverWddm) {
          LOG_DEBUG("NVML: %s runs under WDDM, left to the GPU Engine counters", pci.c_str());
          handle = nullptr;
        }
      }
      it = handles_.emplace(pci, handle).first;
    }
    if (it->second == nullptr || !ComputePids(it->second, pids)) {
      continue;
    }
    // Our own context is on the list too. Normally we spot it by pid; in a
    // PID namespace (container) NVML may report it under the host pid, but
    // it is still exactly one of the entries.
    size_t others = 0;
    int other_pid = 0;
    bool self_seen = false;
    for (int pid : pids) {
      if (pid == self_pid_) {
        self_seen = true;
      } else {
        ++others;
        other_pid = pid;
      }
    }
    if (!self_seen && others > 0) {
      --others;
      other_pid = others > 0 ? other_pid : 0;
    }
    out[i].verdict = others > 0 ? Observation::Verdict::kBusy : Observation::Verdict::kIdle;
    out[i].pid = other_pid;
  }
}

}  // namespace gpu::contention
