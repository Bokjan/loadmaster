#pragma once

#include "errcode.h"

#include <cstdint>

namespace cpu {

struct StatInfo {
  uint64_t user;
  uint64_t nice;
  uint64_t system;
  uint64_t idle;
  uint64_t iowait;
  uint64_t irq;
  uint64_t softirq;
  uint64_t steal;
  uint64_t guest;
  uint64_t guest_nice;
  StatInfo()
      : user(0),
        nice(0),
        system(0),
        idle(0),
        iowait(0),
        irq(0),
        softirq(0),
        steal(0),
        guest(0),
        guest_nice(0) {}
};

int Count();
int GetJiffyMillisecond();
ErrCode GetProcStat(StatInfo &info);

}  // namespace cpu
