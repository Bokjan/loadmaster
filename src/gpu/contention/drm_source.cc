#include "drm_source.h"

#include <cstdlib>
#include <set>
#include <thread>
#include <vector>

#include "linux_fs.h"

#include "gpu/constants.h"
#include "util/log.h"

namespace gpu::contention {

namespace {

// A DRM client seen for the first time has no baseline to measure its
// engine time against: read its fdinfo again this much later instead of
// waiting a whole poll.
constexpr int kDrmBaselineMS = 200;

}  // namespace

DrmSource::DrmSource(std::string root, int self_pid) : root_(std::move(root)), self_pid_(self_pid) {
  const std::string drm = root_ + "/sys/class/drm";
  for (const auto &name : linux_fs::ListDir(drm)) {
    // renderD128, card0 -- not the connectors (card0-HDMI-A-1) or "version".
    const bool render = name.rfind("renderD", 0) == 0;
    const bool card = name.rfind("card", 0) == 0 && name.find('-') == std::string::npos;
    if (!render && !card) {
      continue;
    }
    const std::string pci =
        CanonicalPciBusId(linux_fs::BaseName(linux_fs::ReadLink(drm + "/" + name + "/device")));
    if (pci.empty()) {
      continue;
    }
    const std::string driver =
        linux_fs::BaseName(linux_fs::ReadLink(drm + "/" + name + "/device/driver"));
    if (driver == "nvidia") {
      continue;
    }
    nodes_["/dev/dri/" + name] = Node{pci, driver};
    LOG_DEBUG("DRM: /dev/dri/%s is %s (%s)", name.c_str(), pci.c_str(), driver.c_str());
  }
}

void DrmSource::Sample(const std::vector<DeviceLocator> &devices, std::vector<Observation> &out) {
  // Devices to look at: ours, and not already found busy by another source.
  std::set<std::string> wanted;
  for (size_t i = 0; i < devices.size(); ++i) {
    if (out[i].verdict == Observation::Verdict::kBusy) {
      continue;
    }
    for (const auto &[path, node] : nodes_) {
      if (node.pci == devices[i].pci_bus_id) {
        out[i].verdict = Observation::Verdict::kIdle;
        wanted.insert(node.pci);
      }
    }
  }
  if (wanted.empty()) {
    return;
  }
  auto idle = [&](const std::string &pci) {
    for (size_t i = 0; i < devices.size(); ++i) {
      if (out[i].verdict == Observation::Verdict::kIdle && devices[i].pci_bus_id == pci) {
        return true;
      }
    }
    return false;
  };
  auto mark_busy = [&](const std::string &pci, int pid) {
    for (size_t i = 0; i < devices.size(); ++i) {
      if (out[i].verdict == Observation::Verdict::kIdle && devices[i].pci_bus_id == pci) {
        out[i].verdict = Observation::Verdict::kBusy;
        out[i].pid = pid;
      }
    }
  };
  auto busy_between = [](const Reading &before, const Reading &after) {
    const auto fraction =
        DrmBusyFraction(before.info, after.info,
                        std::chrono::duration<double, std::nano>(after.at - before.at).count());
    return fraction && *fraction * 100.0 >= kGpuYieldMinBusyPercent;
  };

  // DRM clients met for the first time: measured once the scan is done.
  struct Fresh {
    std::pair<int, uint64_t> key;
    std::string fdinfo;
    std::string pci;
  };
  std::vector<Fresh> fresh;
  std::map<std::pair<int, uint64_t>, Reading> next;
  const std::string proc = root_ + "/proc";
  for (int pid : linux_fs::ListNumericDir(proc)) {
    if (pid == self_pid_) {
      continue;
    }
    const std::string pid_dir = proc + "/" + std::to_string(pid);
    // fd -> node, for the nodes of wanted devices this process has open.
    std::map<std::string, const Node *> held;
    const std::string fd_dir = pid_dir + "/fd";
    for (const auto &fd : linux_fs::ListDir(fd_dir)) {
      const auto it = nodes_.find(linux_fs::ReadLink(fd_dir + "/" + fd));
      if (it != nodes_.end() && wanted.count(it->second.pci) != 0) {
        held[fd] = &it->second;
      }
    }
    if (held.empty()) {
      continue;
    }
    const auto maps = linux_fs::ReadFile(pid_dir + "/maps");
    if (!maps) {
      continue;
    }
    for (const auto &[fd, node] : held) {
      if (!MapsComputeRuntime(*maps, node->driver)) {
        continue;
      }
      const std::string fdinfo_path = pid_dir + "/fdinfo/" + fd;
      const auto text = linux_fs::ReadFile(fdinfo_path);
      if (!text) {
        continue;  // closed since the fd scan
      }
      const auto info = ParseDrmFdinfo(*text);
      if (!info || (!info->has_engine_ns && info->cycles.empty())) {
        // No usage stats from this driver (or kernel): the runtime has to
        // do.
        mark_busy(node->pci, pid);
        continue;
      }
      // dup()ed fds share a client; without an id, the fd stands for it.
      const uint64_t client = info->client_id != 0
                                  ? info->client_id
                                  : std::strtoull(fd.c_str(), nullptr, 10) | (uint64_t{1} << 63);
      const std::pair<int, uint64_t> key{pid, client};
      if (next.count(key) != 0) {
        continue;
      }
      const Reading now{*info, Clock::now()};
      next[key] = now;
      const auto before = last_.find(key);
      if (before == last_.end()) {
        fresh.push_back({key, fdinfo_path, node->pci});
      } else if (busy_between(before->second, now)) {
        mark_busy(node->pci, pid);
      }
    }
  }

  // No need to measure clients of devices found busy meanwhile.
  std::erase_if(fresh, [&](const Fresh &f) { return !idle(f.pci); });
  if (!fresh.empty()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kDrmBaselineMS));
    for (const auto &f : fresh) {
      const auto text = linux_fs::ReadFile(f.fdinfo);
      const auto info = text ? ParseDrmFdinfo(*text) : std::nullopt;
      if (!info) {
        continue;  // closed meanwhile
      }
      Reading &baseline = next[f.key];
      const Reading now{*info, Clock::now()};
      if (busy_between(baseline, now)) {
        mark_busy(f.pci, f.key.first);
      }
      baseline = now;
    }
  }
  last_ = std::move(next);
}

}  // namespace gpu::contention
