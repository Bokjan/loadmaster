#include "cpu.h"

#include "constants.h"
#include "global.h"
#include "master.h"
#include "util/log.h"

#include <cstring>

#include <thread>

#include <unistd.h>

namespace cpu {

std::unique_ptr<ResourceManager> CreateResourceManager(const Options &options) {
  return std::move(std::unique_ptr<ResourceManager>(new cpu::CpuResourceManagerSimple(options)));
}

int Count() {
  static int count = static_cast<int>(std::thread::hardware_concurrency());
  return count;
}

int GetJiffyMillisecond() {
  long freq = sysconf(_SC_CLK_TCK);
  return static_cast<int>(kMillisecondsPerSecond / freq);
}

ErrCode GetProcStat(StatInfo &info) {
  ErrCode ret = ErrCode::kProcStatUnknown;
  do {
    char buffer[kSmallBufferLength];
    FILE *fp = fopen("/proc/stat", "r");
    if (fp == nullptr) {
      LOG_E("Failed to open /proc/stat");
      ret = ErrCode::kProcStatOpen;
      break;
    }
    int count = fscanf(fp, "%s%lu%lu%lu%lu%lu%lu%lu%lu%lu%lu", buffer, &info.user, &info.nice,
                       &info.system, &info.idle, &info.iowait, &info.irq, &info.softirq,
                       &info.steal, &info.guest, &info.guest_nice);
    if (count != 11) {
      LOG_E("Failed to `fscanf` from /proc/stat, get val: %d, expect: 11", count);
      ret = ErrCode::kProcStatReadValues;
      break;
    }
    if (strncmp("cpu", buffer, sizeof("cpu") - 1) != 0) {
      LOG_E("Failed to read /proc/stat, have: %s, expect: cpu", buffer);
      ret = ErrCode::kProcStatFindCpuTotal;
      break;
    }
    fclose(fp);
    ret = ErrCode::kOK;
  } while (false);
  if (ret != ErrCode::kOK) {
    global::StopLoop();
  }
  return ret;
}

}  // namespace cpu
