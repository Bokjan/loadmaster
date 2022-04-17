#pragma once

#include <cstdint>

#include <memory>

class Options;
class ResourceManager;

namespace cpu {

struct CpuStatInfo {
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
  CpuStatInfo()
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
bool GetCpuProcStat(CpuStatInfo &info);
void CriticalLoop(int count);

std::unique_ptr<ResourceManager> CreateResourceManager(const Options &options);

}  // namespace cpu
