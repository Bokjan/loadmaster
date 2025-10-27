#pragma once

#include "core/constants.h"

#if IS_WINDOWS

namespace util {

inline uint64_t WindowsEpochToMilliseconds(uint64_t epoch) {
  // 1 epoch = 100 ns,
  return epoch / 10000;
}

inline uint64_t FiletimeTo100Ns(LPFILETIME filetime) {
  ULARGE_INTEGER large_int{};
  large_int.LowPart = filetime->dwLowDateTime;
  large_int.HighPart = filetime->dwHighDateTime;
  return large_int.QuadPart;
}

}  // namespace util

#endif
