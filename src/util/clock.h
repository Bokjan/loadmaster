#pragma once

// We only need IS_WINDOWS here, so include the platform-detection header
// directly rather than core/constants.h. The latter pulls in a much
// larger set of project-wide constants we don't use, and depending on
// it just for a transitive macro is the kind of include-what-you-use
// trap clangd flags as `unused-includes`.
#include "core/platform.h"

namespace util {

#if !IS_WINDOWS
// Kernel jiffy frequency (HZ), derived from sysconf(_SC_CLK_TCK). Cached
// after the first call. Always positive (falls back to 100 if the syscall
// is unavailable).
//
// Callers convert a jiffy DIFF to nanoseconds as
//   diff * 1'000'000'000 / GetJiffyFrequency()
// (with a 128-bit intermediate). Working from the frequency directly --
// rather than a precomputed "milliseconds per jiffy" -- avoids the
// integer-division truncation that lost ~10% precision on HZ=300 and
// collapsed to 0 on HZ >= 2000. The conversion must only be applied to
// small diffs, never to cumulative counters, so the cumulative path
// stays in tick space and cannot wrap (see cpu::ReadSystemBusyTicks).
long GetJiffyFrequency();
#endif

}  // namespace util
