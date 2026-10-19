#include "core/platform.h"

#if IS_LINUX

#  include "stat.h"
#  include "stat_internal.h"

#  include <sched.h>
#  include <unistd.h>

#  include <algorithm>
#  include <cinttypes>
#  include <cstdio>
#  include <cstring>
#  include <memory>
#  include <string>
#  include <string_view>
#  include <thread>
#  include <vector>

#  include "core/constants.h"
#  include "util/clock.h"
#  include "util/log.h"

#  define LMPU64 "%" PRIu64

namespace cpu {

namespace {

// RAII wrapper for FILE*.
struct FileCloser {
  void operator()(FILE *fp) const noexcept {
    if (fp != nullptr) {
      std::fclose(fp);
    }
  }
};
using UniqueFile = std::unique_ptr<FILE, FileCloser>;

}  // namespace

namespace internal {

uint64_t BusyJiffies(const ProcStatFields &f) {
  // "Not idle" -> busy. idle and iowait are excluded; guest/guest_nice are
  // already folded into user/nice by the kernel and must not be re-added.
  return f.user + f.nice + f.system + f.irq + f.softirq + f.steal;
}

uint64_t JiffiesToNs(uint64_t jiffies) {
  // /proc/stat counts in USER_HZ units, i.e. sysconf(_SC_CLK_TCK).
  return util::TicksToNs(jiffies, util::GetClockTicksPerSecond());
}

std::optional<uint64_t> ReadBusyJiffies(FILE *fp, const std::vector<bool> *allowed) {
  char line[kSmallBufferLength * 4];
  uint64_t total = 0;
  bool found = false;
  while (std::fgets(line, sizeof(line), fp) != nullptr) {
    const std::string_view sv(line);
    if (!sv.starts_with("cpu")) {
      break;  // the cpu lines come first; nothing of interest follows
    }
    const bool is_aggregate = sv.size() > 3 && (sv[3] == ' ' || sv[3] == '\t');
    int cpu_id = -1;
    if (!is_aggregate && std::sscanf(line, "cpu%d", &cpu_id) != 1) {
      return std::nullopt;
    }
    if (allowed == nullptr ? !is_aggregate
                           : (is_aggregate || cpu_id < 0 ||
                              static_cast<size_t>(cpu_id) >= allowed->size() ||
                              !(*allowed)[cpu_id])) {
      continue;
    }
    ProcStatFields f{};
    const char *fields = std::strchr(line, ' ');
    constexpr auto kFieldsFormat =
        LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64;
    // guest / guest_nice may be absent on old kernels; the first eight
    // fields are always present.
    if (fields == nullptr ||
        std::sscanf(fields, kFieldsFormat, &f.user, &f.nice, &f.system, &f.idle, &f.iowait,
                    &f.irq, &f.softirq, &f.steal, &f.guest, &f.guest_nice) < 8) {
      return std::nullopt;
    }
    total += BusyJiffies(f);
    found = true;
    if (allowed == nullptr) {
      break;
    }
  }
  if (!found) {
    return std::nullopt;
  }
  return total;
}

std::optional<int> ParseCgroupCpuMax(std::string_view content) {
  long long quota = 0;
  long long period = 0;
  const std::string text(content);
  if (std::sscanf(text.c_str(), "%lld %lld", &quota, &period) != 2 || quota <= 0 || period <= 0) {
    return std::nullopt;  // includes "max <period>": sscanf stops at "max"
  }
  return static_cast<int>((quota + period - 1) / period);
}

}  // namespace internal

namespace {

// The affinity set as a per-CPU mask, or nullopt when we may run on every
// online CPU (then the aggregate "cpu" line is both cheaper and exact).
// Cached: loadmaster does not change its own affinity at runtime.
const std::vector<bool> *AffinityMask() {
  static const std::optional<std::vector<bool>> kCached = []() -> std::optional<std::vector<bool>> {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (::sched_getaffinity(0, sizeof(set), &set) != 0) {
      return std::nullopt;
    }
    const long online = ::sysconf(_SC_NPROCESSORS_ONLN);
    if (online > 0 && CPU_COUNT(&set) >= online) {
      return std::nullopt;
    }
    std::vector<bool> mask(CPU_SETSIZE, false);
    for (int i = 0; i < CPU_SETSIZE; ++i) {
      mask[i] = CPU_ISSET(i, &set);
    }
    return mask;
  }();
  return kCached ? &*kCached : nullptr;
}

}  // namespace

std::optional<uint64_t> ReadSystemBusyNs() {
  UniqueFile fp(std::fopen("/proc/stat", "r"));
  if (!fp) {
    LOG_ERROR("failed to open /proc/stat");
    return std::nullopt;
  }
  const std::optional<uint64_t> busy = internal::ReadBusyJiffies(fp.get(), AffinityMask());
  if (!busy) {
    LOG_ERROR("failed to parse /proc/stat");
    return std::nullopt;
  }
  return internal::JiffiesToNs(*busy);
}

int CoreCount() {
  static const int kCached = []() {
    int count = static_cast<int>(std::thread::hardware_concurrency());
    cpu_set_t set;
    CPU_ZERO(&set);
    if (::sched_getaffinity(0, sizeof(set), &set) == 0 && CPU_COUNT(&set) > 0) {
      count = (count > 0) ? std::min(count, CPU_COUNT(&set)) : CPU_COUNT(&set);
    }
    // cgroup v2 quota (`docker --cpus`). Our cgroup comes from the "0::"
    // line of /proc/self/cgroup; inside a container namespace it is "/".
    std::string cgroup_path = "/";
    if (UniqueFile cg(std::fopen("/proc/self/cgroup", "r")); cg) {
      char line[512];
      while (std::fgets(line, sizeof(line), cg.get()) != nullptr) {
        std::string_view sv(line);
        if (sv.starts_with("0::")) {
          sv.remove_prefix(3);
          while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r')) {
            sv.remove_suffix(1);
          }
          cgroup_path = std::string(sv);
          break;
        }
      }
    }
    // A quota may sit on any ancestor (e.g. a systemd slice), so walk from
    // our cgroup up to the root and honour the tightest one. cgroup v1
    // (cpu.cfs_quota_us) is not consulted.
    while (true) {
      const std::string file =
          "/sys/fs/cgroup" + (cgroup_path == "/" ? std::string() : cgroup_path) + "/cpu.max";
      if (UniqueFile f(std::fopen(file.c_str(), "r")); f) {
        char content[128] = {};
        if (std::fgets(content, sizeof(content), f.get()) != nullptr) {
          if (const auto quota = internal::ParseCgroupCpuMax(content); quota && *quota > 0) {
            count = (count > 0) ? std::min(count, *quota) : *quota;
          }
        }
      }
      if (cgroup_path.empty() || cgroup_path == "/") {
        break;
      }
      const size_t slash = cgroup_path.find_last_of('/');
      cgroup_path = (slash == 0 || slash == std::string::npos) ? "/" : cgroup_path.substr(0, slash);
    }
    return std::max(count, 1);
  }();
  return kCached;
}

}  // namespace cpu

#endif  // IS_LINUX
