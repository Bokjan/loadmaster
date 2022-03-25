#pragma once

#include <memory>
#include <type_traits>

namespace util {

namespace internal {

template <typename T>
using Element = typename std::enable_if<!std::is_array<T>::value, std::unique_ptr<T>>::type;

template <typename T>
using Slice = typename std::enable_if<std::is_array<T>::value && std::extent<T>::value == 0,
                                      std::unique_ptr<T>>::type;

template <typename T>
using Array = typename std::enable_if<std::extent<T>::value != 0, void>::type;

}  // namespace internal

template <typename T, typename... Args>
inline internal::Element<T> make_unique(Args &&...args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

template <typename T>
inline internal::Slice<T> make_unique(size_t size) {
  using U = typename std::remove_extent<T>::type;
  return std::unique_ptr<T>(new U[size]);
}

template <typename T, typename... Args>
internal::Array<T> make_unique(Args &&...) = delete;

}  // namespace util
