#include "proc_stat.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <memory>

#if !IS_WINDOWS
#  include <unistd.h>
#endif

#include "core/constants.h"
#include "util/log.h"
#include "util/win_util.h"

#define LMPU64 "%" PRIu64
#define LMPI64 "%" PRIi64

// Ref: https://man7.org/linux/man-pages/man5/proc.5.html

namespace util {

namespace {

#if !IS_WINDOWS
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

constexpr int kProcStatIntervalMS = 500;
constexpr int kTaskCommLen = 16;
constexpr int kStatFieldsCount = 22;
constexpr int kStatmFieldsCount = 7;

struct StatFields final {
  int pid;
  char comm[kTaskCommLen];
  char state;
  int ppid;
  int pgrp;
  int session;
  int tty_nr;
  int tpgid;
  uint32_t flags;
  uint64_t minflt;
  uint64_t cminflt;
  uint64_t majflt;
  uint64_t cmajflt;
  uint64_t utime;
  uint64_t stime;
  int64_t cutime;
  int64_t cstime;
  int64_t priority;
  int64_t nice;
  int64_t num_threads;
  int64_t iteralvalue;
  uint64_t starttime;
  // ...
};

struct StatmFields final {
  uint64_t size;
  uint64_t resident;
  uint64_t shared;
  uint64_t text;
  uint64_t lib;
  uint64_t data;
  uint64_t dt;
};

#if !IS_WINDOWS
static int GetJiffyMillisecond() {
  static auto ret = []() -> int {
    long freq = sysconf(_SC_CLK_TCK);
    if (freq <= 0) {
      return 10;  // sensible fallback (HZ=100)
    }
    return static_cast<int>(kMillisecondsPerSecond / freq);
  }();
  return ret;
}

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

#if !IS_WINDOWS
// Parse /proc/<pid>/stat into `stat`. Robust against process names that
// contain spaces or parentheses (the `comm` field is wrapped in parens).
//
// Layout: "<pid> (<comm>) <state> <ppid> ..."
// We locate the last ')' to delimit `comm`, then scanf the rest.
static bool ParseProcPidStat(FILE *fp, StatFields &stat) {
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

void ProcStat::UpdateCpuStat(ProcStat::TimePoint now, ForceUpdate force) {
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - time_point_).count();
  if (force != ForceUpdate::kYes && elapsed_ms < kProcStatIntervalMS) {
    return;
  }
  StatFields stat{};
  char file_path[kSmallBufferLength];
  std::snprintf(file_path, sizeof(file_path), "/proc/%d/stat", pid_);
  UniqueFile fp(std::fopen(file_path, "r"));
  if (!fp) {
    LOG_ERROR("failed to open %s", file_path);
    return;
  }
  if (!ParseProcPidStat(fp.get(), stat)) {
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
#else
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

#if !IS_WINDOWS
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
#else
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
