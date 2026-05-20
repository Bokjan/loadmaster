#include "clock.h"

#if !IS_WINDOWS
#  include <unistd.h>
#endif

namespace util {

#if !IS_WINDOWS
int GetJiffyMillisecond() {
  static const int kCached = []() {
    long freq = ::sysconf(_SC_CLK_TCK);
    if (freq <= 0) {
      return 10;  // sensible fallback (HZ=100)
    }
    return static_cast<int>(1000 / freq);
  }();
  return kCached;
}
#endif

}  // namespace util
