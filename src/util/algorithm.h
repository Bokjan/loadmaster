#pragma once

namespace util {

template <typename T>
inline const T &Clamp(const T &value, const T &lower, const T &upper) {
  if (value < lower) {
    return lower;
  }
  if (value > upper) {
    return upper;
  }
  return value;
}

}  // namespace util
