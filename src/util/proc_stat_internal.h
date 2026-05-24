// Linux-only internals of util::ProcStat, exposed in a dedicated header so
// the parsing helper can be exercised directly from unit tests. Do NOT
// include this from production code outside of proc_stat.cc; the public
// surface is `util::ProcStat` in proc_stat.h.
//
// The entire header is compiled out on non-Linux platforms so it cannot
// accidentally be referenced from portable code.

#pragma once

#include "core/platform.h"

#if IS_LINUX

#  include <cstdint>
#  include <cstdio>

namespace util::internal {

// Length of /proc/<pid>/stat field 2 (`comm`), per `man 5 proc`.
constexpr int kTaskCommLen = 16;
// Total number of fields we parse from /proc/<pid>/stat. The kernel
// exposes more, but loadmaster only needs the first 22 (up to
// `starttime`).
constexpr int kStatFieldsCount = 22;

// Subset of /proc/<pid>/stat fields actually consumed by ProcStat. Field
// order matches the kernel's documented layout so a reader can cross-
// reference with `man 5 proc` directly.
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
};

// Parse a single line of /proc/<pid>/stat content from `fp` into `stat`.
//
// Robust against process names that contain spaces and/or parentheses:
// the `comm` field is wrapped in parens, and we locate the LAST ')' to
// delimit it instead of the first. This matters because a malicious or
// merely creative process can rename itself to e.g. "weird ) name".
//
// Returns true on success. On failure (short read, no parens, malformed
// numeric fields) returns false and the contents of `stat` are
// unspecified.
bool ParseProcPidStat(FILE *fp, StatFields &stat);

}  // namespace util::internal

#endif  // IS_LINUX
