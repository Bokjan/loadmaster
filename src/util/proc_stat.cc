#include "proc_stat.h"

#include <cinttypes>
#include <cstdio>

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
  static auto ret = static_cast<int>(kMillisecondsPerSecond / sysconf(_SC_CLK_TCK));
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
void ProcStat::UpdateCpuStat(ProcStat::TimePoint now, ForceUpdate force) {
  // Time diff
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - time_point_).count();
  if (force != ForceUpdate::kYes && elapsed_ms < kProcStatIntervalMS) {
    return;
  }
  // Declare a struct on stack
  StatFields stat;
  // Read from procfs
  char file_path[kSmallBufferLength];
  snprintf(file_path, sizeof(file_path), "/proc/%d/stat", pid_);
  do {
    FILE *fp = fopen(file_path, "r");
    if (fp == nullptr) {
      LOG_FATAL("failed to open %s", file_path);
      break;
    }
    constexpr auto kStatFormat = "%d%s%s%d%d%d%d%d%u" LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPI64 LMPI64
        LMPI64 LMPI64 LMPI64 LMPI64 LMPU64;
    int count =
        fscanf(fp, kStatFormat, &stat.pid, stat.comm, &stat.state, &stat.ppid, &stat.pgrp,
               &stat.session, &stat.tty_nr, &stat.tpgid, &stat.flags, &stat.minflt, &stat.cminflt,
               &stat.majflt, &stat.cmajflt, &stat.utime, &stat.stime, &stat.cutime, &stat.cstime,
               &stat.priority, &stat.nice, &stat.num_threads, &stat.iteralvalue, &stat.starttime);
    if (count != kStatFieldsCount) {
      LOG_FATAL("failed to `fscanf` from %s, get val: %d, expect: %d", file_path, count,
                kStatFieldsCount);
      break;
    }
    fclose(fp);
  } while (false);
  // Calc
  auto jiffies_self = stat.utime + stat.stime;
  auto jiffies_child = stat.cutime + stat.cstime;
  do {
    if (time_point_.time_since_epoch().count() == 0) {
      LOG_DEBUG("first time");
      break;
    }
    auto jiffies_self_diff = jiffies_self - jiffies_self_;
    auto jiffies_child_diff = jiffies_child - jiffies_child_;
    auto jiffies_diff = jiffies_self_diff + jiffies_child_diff;
    auto cpu_ms = jiffies_diff * GetJiffyMillisecond();
    cpu_load_cached_ = static_cast<int>(static_cast<double>(cpu_ms) / elapsed_ms * 100.0);
  } while (false);
  // Update
  time_point_ = now;
  jiffies_self_ = jiffies_self;
  jiffies_child_ = jiffies_child;
}
#else
void ProcStat::UpdateCpuStat(ProcStat::TimePoint now, ForceUpdate force) {
  // Time diff
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - time_point_).count();
  if (force != ForceUpdate::kYes && elapsed_ms < kProcStatIntervalMS) {
    return;
  }
  // Output data
  FILETIME creation, exit, kernel, user;
  if (!GetProcessTimes(process_handle_, &creation, &exit, &kernel, &user)) {
    LOG_FATAL("failed to invoke GetProcessTimes, this=%p", this);
    return;
  }
  // Calc
  auto epoch = FiletimeTo100Ns(&kernel) + FiletimeTo100Ns(&user);
  do {
    if (time_point_.time_since_epoch().count() == 0) {
      LOG_DEBUG("first time in ProcStat::UpdateCpuStat, this=%p", this);
      break;
    }
    auto epoch_diff = epoch - epoch_;
    auto cpu_ms = WindowsEpochToMilliseconds(epoch_diff);
    cpu_load_cached_ = static_cast<int>(static_cast<double>(cpu_ms) / elapsed_ms * 100.0);
  } while (false);
  // Update
  time_point_ = now;
  epoch_ = epoch;
}
#endif

#if !IS_WINDOWS
uint64_t ProcStat::GetMemory() const {
  // Declare a struct on stack
  StatmFields statm;
  // Read from procfs
  char file_path[kSmallBufferLength];
  snprintf(file_path, sizeof(file_path), "/proc/%d/statm", pid_);
  do {
    FILE *fp = fopen(file_path, "r");
    if (fp == nullptr) {
      LOG_FATAL("failed to open %s", file_path);
      break;
    }
    constexpr auto kMemoryFormat = LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64;
    int count = fscanf(fp, kMemoryFormat, &statm.size, &statm.resident, &statm.shared, &statm.text,
                       &statm.lib, &statm.data, &statm.dt);
    if (count != kStatmFieldsCount) {
      LOG_FATAL("failed to `fscanf` from %s, get val: %d, expect: %d", file_path, count,
                kStatmFieldsCount);
      break;
    }
    fclose(fp);
  } while (false);
  // Page count to byte count
  return static_cast<uint64_t>(statm.size * GetPageSize());
}
#else
uint64_t ProcStat::GetMemory() const {
  APP_MEMORY_INFORMATION mem_info;
  constexpr auto info_cls = ProcessAppMemoryInfo;
  if (!GetProcessInformation(process_handle_, info_cls, &mem_info, sizeof(mem_info))) {
    LOG_FATAL("failed to invoke GetProcessInformation, this=%p", this);
    return 0;
  }
  return mem_info.TotalCommitUsage;
}
#endif

}  // namespace util
