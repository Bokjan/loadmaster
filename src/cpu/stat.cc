#include "stat.h"

#include <stdexcept>
#include <string_view>

#include <unistd.h>

#include "constants.h"
#include "util/log.h"

namespace cpu {

int GetJiffyMillisecond() {
  long freq = sysconf(_SC_CLK_TCK);
  return static_cast<int>(kMillisecondsPerSecond / freq);
}

bool GetCpuProcStat(CpuStatInfo &info) {
  bool ret = false;
  do {
    char buffer[kSmallBufferLength];
    FILE *fp = fopen("/proc/stat", "r");
    if (fp == nullptr) {
      LOG_ERROR("failed to open /proc/stat");
      break;
    }
    int count = fscanf(fp, "%s%lu%lu%lu%lu%lu%lu%lu%lu%lu%lu", buffer, &info.user, &info.nice,
                       &info.system, &info.idle, &info.iowait, &info.irq, &info.softirq,
                       &info.steal, &info.guest, &info.guest_nice);
    if (count != 11) {
      LOG_ERROR("failed to `fscanf` from /proc/stat, get val: %d, expect: 11", count);
      break;
    }
    if (!std::string_view(buffer).starts_with("cpu")) {
      LOG_ERROR("failed to read /proc/stat, have: %s, expect: cpu", buffer);
      break;
    }
    fclose(fp);
    ret = true;
  } while (false);
  if (!ret) {
    throw std::runtime_error("failed to read /proc/stat");
  }
  return ret;
}

}  // namespace cpu
