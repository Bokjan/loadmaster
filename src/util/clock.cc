#include "clock.h"

#if !IS_WINDOWS
#  include <unistd.h>
#endif

namespace util {

#if !IS_WINDOWS
long GetJiffyFrequency() {
  static const long kCached = []() {
    long freq = ::sysconf(_SC_CLK_TCK);
    if (freq <= 0) {
      return 100L;  // sensible fallback (HZ=100)
    }
    return freq;
  }();
  return kCached;
}
#endif

}  // namespace util
