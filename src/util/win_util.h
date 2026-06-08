#pragma once

#include "core/platform.h"

#if IS_WINDOWS

#  include <cstdint>

namespace util {

inline uint64_t FiletimeTo100Ns(LPFILETIME filetime) {
  ULARGE_INTEGER large_int{};
  large_int.LowPart = filetime->dwLowDateTime;
  large_int.HighPart = filetime->dwHighDateTime;
  return large_int.QuadPart;
}

}  // namespace util

#endif
