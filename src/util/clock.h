#pragma once

#include "core/constants.h"

namespace util {

#if !IS_WINDOWS
// Length of one jiffy in milliseconds, derived from sysconf(_SC_CLK_TCK).
// Cached after the first call. Always returns a positive value (falls back
// to 10 ms / HZ=100 if the syscall is unavailable).
int GetJiffyMillisecond();
#endif

}  // namespace util
