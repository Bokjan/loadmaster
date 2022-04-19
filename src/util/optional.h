#pragma once

#include <utility>

namespace util {

template <typename T>
class optional final {
 public:
  optional() = default;
  optional(const optional &) = default;
  optional &operator=(const optional &) = default;
  optional(optional &&) = default;
  optional &operator=(optional &&) = default;
  ~optional() = default;

  template <typename... Args>
  explicit optional(Args &&...args) : value_(true, T(std::forward<Args>(args)...)) {}

  optional<T> &operator=(T &&val) {
    value_.first = true;
    value_.second = val;
    return *this;
  }

  optional<T> &operator=(T &val) {
    value_.first = true;
    value_.second = std::move(val);
    return *this;
  }

  explicit operator bool() const { return value_.first; }
  T &value() { return value_.second; }
  const T &value() const { return value_.second; }
  T *operator->() { return &value_.second; }
  const T *operator->() const { return &value_.second; }
  T &operator*() { return value_.second; }
  const T &operator*() const { return value_.second; }

 private:
  std::pair<bool, T> value_;
};

}  // namespace util
