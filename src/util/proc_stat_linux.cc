#include "core/platform.h"

#if IS_LINUX

#  include "proc_stat.h"
#  include "proc_stat_internal.h"

#  include <algorithm>
#  include <cinttypes>
#  include <cstdio>
#  include <cstring>
#  include <memory>

#  include "core/constants.h"
#  include "util/clock.h"
#  include "util/log.h"

#  define LMPU64 "%" PRIu64
#  define LMPI64 "%" PRIi64

// Ref: https://man7.org/linux/man-pages/man5/proc.5.html

namespace util {

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

std::optional<uint64_t> ProcStat::ReadProcessCpuTicks() const {
  internal::StatFields stat{};
  char file_path[kSmallBufferLength];
  std::snprintf(file_path, sizeof(file_path), "/proc/%d/stat", pid_);
  UniqueFile fp(std::fopen(file_path, "r"));
  if (!fp) {
    LOG_ERROR("failed to open %s", file_path);
    return std::nullopt;
  }
  if (!internal::ParseProcPidStat(fp.get(), stat)) {
    LOG_ERROR("failed to parse %s", file_path);
    return std::nullopt;
  }
  // Self + reaped-children user/system jiffies (matches historical behavior).
  // Returned as raw jiffies -- the native unit -- so the caller diffs in
  // tick space (never wrapping in realistic uptime) and only converts the
  // small diff via ProcessCpuTicksToNs. Returning cumulative ns here would
  // overflow uint64 after ~9 years on a process pegging 64 cores.
  const int64_t jiffies = static_cast<int64_t>(stat.utime) + static_cast<int64_t>(stat.stime) +
                          stat.cutime + stat.cstime;
  const int64_t clamped = jiffies < 0 ? 0 : jiffies;
  return static_cast<uint64_t>(clamped);
}

uint64_t ProcessCpuTicksToNs(uint64_t tick_diff) {
  // jiffy -> ns: diff * 1e9 / HZ with a 128-bit intermediate. Working from
  // the frequency (not a precomputed ms-per-jiffy) avoids the ~10% low-bias
  // on HZ=300 and the collapse to 0 on HZ>=2000 that the old
  // GetJiffyMillisecond()*1e6 path had.
  return static_cast<uint64_t>((static_cast<__uint128_t>(tick_diff) * 1'000'000'000ULL) /
                               static_cast<uint64_t>(GetJiffyFrequency()));
}

}  // namespace util

#endif  // IS_LINUX
