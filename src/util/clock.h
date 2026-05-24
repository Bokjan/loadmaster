#pragma once

// We only need IS_WINDOWS here, so include the platform-detection header
// directly rather than core/constants.h. The latter pulls in a much
// larger set of project-wide constants we don't use, and depending on
// it just for a transitive macro is the kind of include-what-you-use
// trap clangd flags as `unused-includes`.
#include "core/platform.h"

namespace util {

#if !IS_WINDOWS
// Length of one jiffy in milliseconds, derived from sysconf(_SC_CLK_TCK).
// Cached after the first call. Always returns a positive value (falls back
// to 10 ms / HZ=100 if the syscall is unavailable).
int GetJiffyMillisecond();
#endif

}  // namespace util
