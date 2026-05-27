#include "proc_stat.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <memory>

#if !IS_WINDOWS
#  include <unistd.h>
#endif

#if IS_MACOS
#  include <libproc.h>
#  include <sys/resource.h>
#endif

#include "core/constants.h"
#include "util/clock.h"
#include "util/log.h"
#include "util/proc_stat_internal.h"
#include "util/win_util.h"

#define LMPU64 "%" PRIu64
#define LMPI64 "%" PRIi64

// Ref: https://man7.org/linux/man-pages/man5/proc.5.html

namespace util {

namespace {

#if IS_LINUX
// RAII wrapper for FILE*.
struct FileCloser {
  void operator()(FILE *fp) const noexcept {
    if (fp != nullptr) {
      std::fclose(fp);
    }
  }
};
using UniqueFile = std::unique_ptr<FILE, FileCloser>;
#endif

}  // namespace

// Used by all platforms to throttle the per-process stat refresh rate.
constexpr int kProcStatIntervalMS = 500;

#if IS_LINUX
// /proc/<pid>/statm parsing helper. The /proc/<pid>/stat layout lives in
// proc_stat_internal.h so it can be exercised by unit tests; statm is
// trivial enough that we keep it inline.
constexpr int kStatmFieldsCount = 7;

struct StatmFields final {
  uint64_t size;
  uint64_t resident;
  uint64_t shared;
  uint64_t text;
  uint64_t lib;
  uint64_t data;
  uint64_t dt;
};

static int GetPageSize() {
  static auto ret = sysconf(_SC_PAGE_SIZE);
  return ret;
}
#endif

ProcStat::ProcStat() {
#if !IS_WINDOWS
  pid_ = getpid();
#else
  process_handle_ = GetCurrentProcess();
#endif
}

#if IS_LINUX
namespace internal {

// Definition declared in proc_stat_internal.h. Lives here so unit tests
// can link against it directly without having to drag in any platform-
// gated code.
bool ParseProcPidStat(FILE *fp, StatFields &stat) {
  char raw[kSmallBufferLength * 4];
  if (std::fgets(raw, sizeof(raw), fp) == nullptr) {
    return false;
  }
  // Locate "(" and the LAST ")" to safely extract comm.
  char *lparen = std::strchr(raw, '(');
  char *rparen = std::strrchr(raw, ')');
  if (lparen == nullptr || rparen == nullptr || rparen <= lparen) {
    return false;
  }
  // pid: everything before '('
  *lparen = '\0';
  if (std::sscanf(raw, "%d", &stat.pid) != 1) {
    return false;
  }
  // comm: between '(' and ')'
  const size_t comm_len = static_cast<size_t>(rparen - lparen - 1);
  const size_t copy_len = std::min<size_t>(comm_len, sizeof(stat.comm) - 1);
  std::memcpy(stat.comm, lparen + 1, copy_len);
  stat.comm[copy_len] = '\0';
  // Remaining fields after ')'
  char *rest = rparen + 1;
  constexpr auto kRestFormat = " %c %d %d %d %d %d %u" LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64
      LMPI64 LMPI64 LMPI64 LMPI64 LMPI64 LMPI64 LMPU64;
  const int count = std::sscanf(rest, kRestFormat, &stat.state, &stat.ppid, &stat.pgrp,
                                &stat.session, &stat.tty_nr, &stat.tpgid, &stat.flags, &stat.minflt,
                                &stat.cminflt, &stat.majflt, &stat.cmajflt, &stat.utime,
                                &stat.stime, &stat.cutime, &stat.cstime, &stat.priority, &stat.nice,
                                &stat.num_threads, &stat.iteralvalue, &stat.starttime);
  // pid + comm + 20 remaining = 22 total, expect 20 from sscanf here.
  return count == kStatFieldsCount - 2;
}

}  // namespace internal

void ProcStat::UpdateCpuStat(ProcStat::TimePoint now, ForceUpdate force) {
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - time_point_).count();
  if (force != ForceUpdate::kYes && elapsed_ms < kProcStatIntervalMS) {
    return;
  }
  internal::StatFields stat{};
  char file_path[kSmallBufferLength];
  std::snprintf(file_path, sizeof(file_path), "/proc/%d/stat", pid_);
  UniqueFile fp(std::fopen(file_path, "r"));
  if (!fp) {
    LOG_ERROR("failed to open %s", file_path);
    return;
  }
  if (!internal::ParseProcPidStat(fp.get(), stat)) {
    LOG_ERROR("failed to parse %s", file_path);
    return;
  }
  const auto jiffies_self = stat.utime + stat.stime;
  const auto jiffies_child = stat.cutime + stat.cstime;
  if (time_point_.time_since_epoch().count() == 0) {
    LOG_DEBUG("first time");
  } else {
    const auto jiffies_self_diff = jiffies_self - jiffies_self_;
    const auto jiffies_child_diff = jiffies_child - jiffies_child_;
    const auto jiffies_diff = jiffies_self_diff + jiffies_child_diff;
    const auto cpu_ms = static_cast<int64_t>(jiffies_diff) * GetJiffyMillisecond();
    if (elapsed_ms > 0) {
      cpu_load_cached_ = static_cast<int>(static_cast<double>(cpu_ms) / elapsed_ms * 100.0);
    }
  }
  time_point_ = now;
  jiffies_self_ = jiffies_self;
  jiffies_child_ = jiffies_child;
}
#elif IS_MACOS
// macOS has no /proc; query libproc for absolute CPU time of this process.
// `rusage_info_v2` exposes ri_user_time / ri_system_time in nanoseconds of
// machine time, accumulated across the entire process tree we care about
// here (current process + already-reaped children's time is already folded
// in by the kernel).
void ProcStat::UpdateCpuStat(ProcStat::TimePoint now, ForceUpdate force) {
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - time_point_).count();
  if (force != ForceUpdate::kYes && elapsed_ms < kProcStatIntervalMS) {
    return;
  }
  rusage_info_current rusage{};
  if (::proc_pid_rusage(pid_, RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t *>(&rusage)) != 0) {
    LOG_ERROR("proc_pid_rusage failed for pid %d", pid_);
    return;
  }
  // Both are uint64_t nanoseconds; combine into a single "self CPU ns"
  // counter. We reuse jiffies_self_ as the running total (in ns here);
  // its semantics on macOS differ from Linux but it's only used as a
  // monotonic baseline for diffs below, never exposed.
  const uint64_t cpu_ns_total = rusage.ri_user_time + rusage.ri_system_time;
  if (time_point_.time_since_epoch().count() == 0) {
    LOG_DEBUG("first time");
  } else {
    const uint64_t cpu_ns_diff = cpu_ns_total - jiffies_self_;
    const auto cpu_ms = static_cast<int64_t>(cpu_ns_diff / 1'000'000ULL);
    if (elapsed_ms > 0) {
      cpu_load_cached_ = static_cast<int>(static_cast<double>(cpu_ms) / elapsed_ms * 100.0);
    }
  }
  time_point_ = now;
  jiffies_self_ = cpu_ns_total;
  jiffies_child_ = 0;  // unused on macOS
}
#else  // IS_WINDOWS
void ProcStat::UpdateCpuStat(ProcStat::TimePoint now, ForceUpdate force) {
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - time_point_).count();
  if (force != ForceUpdate::kYes && elapsed_ms < kProcStatIntervalMS) {
    return;
  }
  FILETIME creation, exit, kernel, user;
  if (!GetProcessTimes(process_handle_, &creation, &exit, &kernel, &user)) {
    LOG_ERROR("failed to invoke GetProcessTimes, this=%p", this);
    return;
  }
  const auto epoch = FiletimeTo100Ns(&kernel) + FiletimeTo100Ns(&user);
  if (time_point_.time_since_epoch().count() == 0) {
    LOG_DEBUG("first time in ProcStat::UpdateCpuStat, this=%p", this);
  } else {
    const auto epoch_diff = epoch - epoch_;
    const auto cpu_ms = WindowsEpochToMilliseconds(epoch_diff);
    if (elapsed_ms > 0) {
      cpu_load_cached_ = static_cast<int>(static_cast<double>(cpu_ms) / elapsed_ms * 100.0);
    }
  }
  time_point_ = now;
  epoch_ = epoch;
}
#endif

#if IS_LINUX
uint64_t ProcStat::GetMemory() const {
  StatmFields statm{};
  char file_path[kSmallBufferLength];
  std::snprintf(file_path, sizeof(file_path), "/proc/%d/statm", pid_);
  UniqueFile fp(std::fopen(file_path, "r"));
  if (!fp) {
    LOG_ERROR("failed to open %s", file_path);
    return 0;
  }
  constexpr auto kMemoryFormat = LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64;
  const int count = std::fscanf(fp.get(), kMemoryFormat, &statm.size, &statm.resident,
                                &statm.shared, &statm.text, &statm.lib, &statm.data, &statm.dt);
  if (count != kStatmFieldsCount) {
    LOG_ERROR("failed to `fscanf` from %s, get val: %d, expect: %d", file_path, count,
              kStatmFieldsCount);
    return 0;
  }
  return static_cast<uint64_t>(statm.size * GetPageSize());
}
#elif IS_MACOS
uint64_t ProcStat::GetMemory() const {
  rusage_info_current rusage{};
  if (::proc_pid_rusage(pid_, RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t *>(&rusage)) != 0) {
    LOG_ERROR("proc_pid_rusage failed for pid %d", pid_);
    return 0;
  }
  // ri_resident_size is already in bytes.
  return rusage.ri_resident_size;
}
#else  // IS_WINDOWS
uint64_t ProcStat::GetMemory() const {
  APP_MEMORY_INFORMATION mem_info;
  constexpr auto info_cls = ProcessAppMemoryInfo;
  if (!GetProcessInformation(process_handle_, info_cls, &mem_info, sizeof(mem_info))) {
    LOG_ERROR("failed to invoke GetProcessInformation, this=%p", this);
    return 0;
  }
  return mem_info.TotalCommitUsage;
}
#endif

}  // namespace util
