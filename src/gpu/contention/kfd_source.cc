#include "kfd_source.h"

#include <cstdlib>

#include "linux_fs.h"
#include "parse.h"

#include "util/log.h"

namespace gpu::contention {

namespace {

constexpr const char *kTopologyNodes = "/sys/class/kfd/kfd/topology/nodes";
constexpr const char *kProcs = "/sys/class/kfd/kfd/proc";

// "dddd:bb:dd.f" -> "dddd:bb:dd.0"; see gpu_ids_.
std::string WithoutFunction(std::string pci) {
  if (!pci.empty()) {
    pci.back() = '0';
  }
  return pci;
}

}  // namespace

KfdSource::KfdSource(std::string root, int self_pid) : root_(std::move(root)), self_pid_(self_pid) {
  const std::string nodes = root_ + kTopologyNodes;
  for (int node : linux_fs::ListNumericDir(nodes)) {
    const std::string dir = nodes + "/" + std::to_string(node);
    const auto gpu_id_text = linux_fs::ReadFile(dir + "/gpu_id");
    const auto properties = linux_fs::ReadFile(dir + "/properties");
    if (!gpu_id_text || !properties) {
      continue;
    }
    const auto gpu_id = static_cast<uint32_t>(std::strtoul(gpu_id_text->c_str(), nullptr, 10));
    const auto domain = KfdProperty(*properties, "domain");
    const auto location = KfdProperty(*properties, "location_id");
    if (gpu_id == 0 || !location) {
      continue;  // CPU node
    }
    const std::string pci = WithoutFunction(
        KfdPciBusId(static_cast<uint32_t>(domain.value_or(0)), static_cast<uint32_t>(*location)));
    gpu_ids_[pci].insert(gpu_id);
    LOG_DEBUG("amdkfd: node %d is gpu_id %u at %s", node, gpu_id, pci.c_str());
  }
}

bool KfdSource::Available(const std::string &root) {
  return !linux_fs::ListDir(root + kTopologyNodes).empty();
}

void KfdSource::Sample(const std::vector<DeviceLocator> &devices, std::vector<Observation> &out) {
  // gpu_id -> pids holding a queue on it. (Secondary contexts, newer
  // kernels' proc/<pid>/context_<n>/queues, belong to the same pid and
  // are left out: the primary one is enough to see the process.)
  std::map<uint32_t, std::set<int>> owners;
  bool self_seen = false;
  const std::string procs = root_ + kProcs;
  for (int pid : linux_fs::ListNumericDir(procs)) {
    if (pid == self_pid_) {
      self_seen = true;
      continue;
    }
    const std::string queues = procs + "/" + std::to_string(pid) + "/queues";
    for (int queue : linux_fs::ListNumericDir(queues)) {
      const auto text = linux_fs::ReadFile(queues + "/" + std::to_string(queue) + "/gpuid");
      if (text) {
        owners[static_cast<uint32_t>(std::strtoul(text->c_str(), nullptr, 10))].insert(pid);
      }
    }
  }
  std::set<std::string> busy_pcis;
  for (size_t i = 0; i < devices.size(); ++i) {
    const auto it = gpu_ids_.find(WithoutFunction(devices[i].pci_bus_id));
    if (devices[i].pci_bus_id.empty() || it == gpu_ids_.end()) {
      continue;
    }
    std::set<int> pids;
    for (uint32_t gpu_id : it->second) {
      const auto &owner_pids = owners[gpu_id];
      pids.insert(owner_pids.begin(), owner_pids.end());
    }
    size_t others = pids.size();
    // amdkfd names the directories by global pid. Inside a PID namespace
    // (container) our own one doesn't match getpid(), so it sits in `pids`
    // as an anonymous entry: discount it when we know we are a client.
    if (!self_seen && devices[i].kfd_client && others > 0) {
      --others;
    }
    out[i].verdict = others > 0 ? Observation::Verdict::kBusy : Observation::Verdict::kIdle;
    out[i].pid = others > 0 ? *pids.begin() : 0;
    if (others > 0) {
      busy_pcis.insert(devices[i].pci_bus_id);
    }
  }
  // amdkfd has no per-process usage; amdgpu has the whole device's.
  if (busy_pcis.empty()) {
    return;
  }
  const auto busy = linux_fs::AverageGpuBusyPercent(root_, busy_pcis);
  for (size_t i = 0; i < devices.size(); ++i) {
    const auto it = busy.find(devices[i].pci_bus_id);
    if (out[i].verdict == Observation::Verdict::kBusy && it != busy.end()) {
      out[i].device_busy = it->second;
    }
  }
}

}  // namespace gpu::contention
