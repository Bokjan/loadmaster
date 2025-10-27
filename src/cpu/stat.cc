#include "stat.h"

#include <cinttypes>
#include <stdexcept>
#include <string_view>

#include "core/constants.h"
#include "util/log.h"
#include "util/win_util.h"

#if !IS_WINDOWS
#  include <unistd.h>
#endif
namespace cpu {

#if !IS_WINDOWS
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
    auto kStatFormat = "%s" PRIu64 PRIu64 PRIu64 PRIu64 PRIu64 PRIu64 PRIu64 PRIu64 PRIu64 PRIu64;
    int count =
        fscanf(fp, kStatFormat, buffer, &info.user, &info.nice, &info.system, &info.idle,
               &info.iowait, &info.irq, &info.softirq, &info.steal, &info.guest, &info.guest_nice);
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
#else
bool GetCpuProcStat(CpuStatInfo &info) {
  FILETIME idle, kernel, user;
  if (!GetSystemTimes(&idle, &kernel, &user)) {
    LOG_FATAL("failed to invoke GetSystemTimes");
    return false;
  }
  uint64_t idle_100ns = util::FiletimeTo100Ns(&idle);
  uint64_t kernel_100ns = util::FiletimeTo100Ns(&kernel);
  uint64_t user_100ns = util::FiletimeTo100Ns(&user);

  info.windows_concerned_100ns_ = user_100ns + (kernel_100ns - idle_100ns);

  // LOG_TRACE("GetSystemTimes idle=%llu, kernel=%llu, user=%llu, concerned=%llu",
  //           idle_100ns, kernel_100ns, user_100ns, info.windows_concerned_100ns_);
  return true;
}
#endif

}  // namespace cpu
