#include "stat.h"

#include <cinttypes>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string_view>

#include "core/constants.h"
#include "util/clock.h"
#include "util/log.h"
#include "util/win_util.h"

#if IS_MACOS
#  include <mach/mach.h>
#  include <mach/mach_host.h>
#endif

#define LMPU64 "%" PRIu64

namespace cpu {

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

#if IS_LINUX
bool GetCpuProcStat(CpuStatInfo &info) {
  // Constrain `%s` width to avoid buffer overflow.
  char buffer[kSmallBufferLength];
  UniqueFile fp(std::fopen("/proc/stat", "r"));
  if (!fp) {
    LOG_ERROR("failed to open /proc/stat");
    throw std::runtime_error("failed to open /proc/stat");
  }
  constexpr auto kStatFormat =
      "%127s" LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64;
  const int count = std::fscanf(fp.get(), kStatFormat, buffer, &info.user, &info.nice, &info.system,
                                &info.idle, &info.iowait, &info.irq, &info.softirq, &info.steal,
                                &info.guest, &info.guest_nice);
  if (count != 11) {
    LOG_ERROR("failed to `fscanf` from /proc/stat, get val: %d, expect: 11", count);
    throw std::runtime_error("malformed /proc/stat");
  }
  if (!std::string_view(buffer).starts_with("cpu")) {
    LOG_ERROR("failed to read /proc/stat, have: %s, expect: cpu", buffer);
    throw std::runtime_error("unexpected /proc/stat content");
  }
  return true;
}

uint64_t GetBusyTicks(const CpuStatInfo &info) {
  // Match the historical "concerned" definition: count user-space, niced
  // user-space, and kernel-space time only. Idle/iowait/steal etc. are
  // explicitly excluded so that "system load" reflects what loadmaster's
  // controller cares about.
  return info.user + info.nice + info.system;
}

uint64_t TicksToMilliseconds(uint64_t ticks) {
  return ticks * static_cast<uint64_t>(util::GetJiffyMillisecond());
}
#elif IS_MACOS
// macOS has no /proc; query the Mach host for system-wide CPU tick counters.
// host_cpu_load_info_data_t reports user / system / idle / nice ticks summed
// across all CPUs, expressed in the same unit as Linux jiffies (driven by
// sysconf(_SC_CLK_TCK), typically 100 Hz on macOS), so the rest of the
// pipeline (GetBusyTicks / TicksToMilliseconds) works unchanged.
bool GetCpuProcStat(CpuStatInfo &info) {
  host_cpu_load_info_data_t load{};
  mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
  const kern_return_t kr = ::host_statistics64(::mach_host_self(), HOST_CPU_LOAD_INFO,
                                               reinterpret_cast<host_info64_t>(&load), &count);
  if (kr != KERN_SUCCESS) {
    LOG_ERROR("host_statistics64(HOST_CPU_LOAD_INFO) failed: kr=%d", kr);
    throw std::runtime_error("host_statistics64 failed");
  }
  info.user       = load.cpu_ticks[CPU_STATE_USER];
  info.nice       = load.cpu_ticks[CPU_STATE_NICE];
  info.system     = load.cpu_ticks[CPU_STATE_SYSTEM];
  info.idle       = load.cpu_ticks[CPU_STATE_IDLE];
  info.iowait     = 0;  // not reported by Mach
  info.irq        = 0;
  info.softirq    = 0;
  info.steal      = 0;
  info.guest      = 0;
  info.guest_nice = 0;
  return true;
}

uint64_t GetBusyTicks(const CpuStatInfo &info) {
  return info.user + info.nice + info.system;
}

uint64_t TicksToMilliseconds(uint64_t ticks) {
  return ticks * static_cast<uint64_t>(util::GetJiffyMillisecond());
}
#else  // IS_WINDOWS
bool GetCpuProcStat(CpuStatInfo &info) {
  FILETIME idle, kernel, user;
  if (!GetSystemTimes(&idle, &kernel, &user)) {
    LOG_ERROR("failed to invoke GetSystemTimes");
    return false;
  }
  uint64_t idle_100ns = util::FiletimeTo100Ns(&idle);
  uint64_t kernel_100ns = util::FiletimeTo100Ns(&kernel);
  uint64_t user_100ns = util::FiletimeTo100Ns(&user);

  info.windows_concerned_100ns = user_100ns + (kernel_100ns - idle_100ns);
  return true;
}

uint64_t GetBusyTicks(const CpuStatInfo &info) { return info.windows_concerned_100ns; }

uint64_t TicksToMilliseconds(uint64_t ticks) { return util::WindowsEpochToMilliseconds(ticks); }
#endif

}  // namespace cpu
