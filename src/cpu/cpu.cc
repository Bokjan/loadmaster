#include "cpu.h"

#include "constants.h"
#include "global.h"
#include "manager_default.h"
#include "manager_random_normal.h"
#include "options.h"
#include "util/log.h"

#include <cstring>

#include <random>
#include <thread>

#include <unistd.h>

namespace cpu {

std::unique_ptr<ResourceManager> CreateResourceManager(const Options &options) {
  std::unique_ptr<ResourceManager> ret;
  switch (options.cpu_algorithm_) {
    case Options::ScheduleAlgorithm::kDefault:
      ret =
          std::move(std::unique_ptr<ResourceManager>(new cpu::CpuResourceManagerDefault(options)));
      break;
    case Options::ScheduleAlgorithm::kRandomNormal:
      ret = std::move(
          std::unique_ptr<ResourceManager>(new cpu::CpuResourceManagerRandomNormal(options)));
      break;

    default:
      LOG_FATAL("invalid CPU algorithm type [%d]", static_cast<int>(options.cpu_algorithm_));
      break;
  }
  return std::move(ret);
}

int Count() {
  static int count = static_cast<int>(std::thread::hardware_concurrency());
  return count;
}

int GetJiffyMillisecond() {
  long freq = sysconf(_SC_CLK_TCK);
  return static_cast<int>(kMillisecondsPerSecond / freq);
}

bool GetProcStat(StatInfo &info) {
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
    global::StopLoop();
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
