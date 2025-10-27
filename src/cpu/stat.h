#pragma once

#include <cstdint>

#include <thread>

#include "core/constants.h"

namespace cpu {

inline int CoreCount() { return static_cast<int>(std::thread::hardware_concurrency()); }

struct CpuStatInfo {
#if !IS_WINDOWS
  uint64_t user = 0;
  uint64_t nice = 0;
  uint64_t system = 0;
  uint64_t idle = 0;
  uint64_t iowait = 0;
  uint64_t irq = 0;
  uint64_t softirq = 0;
  uint64_t steal = 0;
  uint64_t guest = 0;
  uint64_t guest_nice = 0;
#else
  uint64_t windows_concerned_100ns_ = 0;
#endif
};

#if !IS_WINDOWS
int GetJiffyMillisecond();
#endif
bool GetCpuProcStat(CpuStatInfo &info);

}  // namespace cpu
