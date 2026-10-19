#include "clock.h"

#if !IS_WINDOWS
#  include <unistd.h>
#endif

namespace util {

#if !IS_WINDOWS
uint64_t GetClockTicksPerSecond() {
  static const uint64_t kCached = []() -> uint64_t {
    const long freq = ::sysconf(_SC_CLK_TCK);
    if (freq <= 0) {
      return 100;  // sensible fallback (USER_HZ=100)
    }
    return static_cast<uint64_t>(freq);
  }();
  return kCached;
}
#endif

}  // namespace util
