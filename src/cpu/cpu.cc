#include "cpu/cpu.h"

#include <cstring>

#include <random>
#include <stdexcept>
#include <thread>

#include <unistd.h>

#include "constants.h"
#include "core/options.h"
#include "cpu/manager_default.h"
#include "cpu/manager_random_normal.h"
#include "util/log.h"

namespace cpu {

std::unique_ptr<ResourceManager> CreateResourceManager(const Options &options) {
  std::unique_ptr<ResourceManager> ret;
  switch (options.GetCpuAlgorithm()) {
    case Options::CpuAlgorithm::kDefault:
      ret = std::make_unique<CpuResourceManagerDefault>(options);
      break;
    case Options::CpuAlgorithm::kRandomNormal:
      ret = std::make_unique<CpuResourceManagerRandomNormal>(options);
      break;
    default:
      LOG_FATAL("invalid CPU algorithm type [%d]", EnumToInt(options.GetCpuAlgorithm()));
      break;
  }
  return ret;
}

int Count() {
  static int count = static_cast<int>(std::thread::hardware_concurrency());
  return count;
}

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
    if (strncmp("cpu", buffer, sizeof("cpu") - 1) != 0) {
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

void CriticalLoop(int count) {
  std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<> udist(0, count - 1);
  int factor = udist(gen);
  thread_local int val = udist(gen);
  for (int i = 0; i < count; ++i) {
    val *= factor;
  }
}

}  // namespace cpu
