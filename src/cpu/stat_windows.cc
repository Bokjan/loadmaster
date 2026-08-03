#include "core/platform.h"

#if IS_WINDOWS

#  include "stat.h"

#  include "util/log.h"
#  include "util/win_util.h"

namespace cpu {

// Windows reports cumulative idle / kernel / user time as FILETIMEs (100ns
// ticks). Crucially the kernel bucket INCLUDES idle, so the busy time is
// user + (kernel - idle): everything the system was not idle for. Returned
// in native 100ns ticks; BusyTicksToNs scales a diff by 100.
std::optional<uint64_t> ReadSystemBusyTicks() {
  FILETIME idle, kernel, user;
  if (!GetSystemTimes(&idle, &kernel, &user)) {
    LOG_ERROR("failed to invoke GetSystemTimes");
    return std::nullopt;
  }
  const uint64_t idle_100ns = util::FiletimeTo100Ns(&idle);
  const uint64_t kernel_100ns = util::FiletimeTo100Ns(&kernel);
  const uint64_t user_100ns = util::FiletimeTo100Ns(&user);

  // kernel >= idle always holds (idle is a subset of kernel time), but guard
  // anyway so a transient counter glitch can't underflow the unsigned diff.
  const uint64_t busy_kernel_100ns = (kernel_100ns >= idle_100ns) ? (kernel_100ns - idle_100ns) : 0;
  return user_100ns + busy_kernel_100ns;
}

uint64_t BusyTicksToNs(uint64_t tick_diff) {
  // 100ns ticks -> ns.
  return tick_diff * 100ULL;
}

}  // namespace cpu

#endif  // IS_WINDOWS
