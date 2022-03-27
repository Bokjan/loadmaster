#pragma once

#include <utility>

namespace util {

template <typename T>
class optinal final {
 public:
  optinal() = default;
  optinal(const optinal &) = default;
  optinal &operator=(const optinal &) = default;
  optinal(optinal &&) = default;
  optinal &operator=(optinal &&) = default;
  ~optinal() = default;

  template <typename... Args>
  explicit optinal(Args &&...args) : value_(true, T(std::forward<Args>(args)...)) {}

  optinal<T> &operator=(T &&val) {
    value_.first = true;
    value_.second = val;
    return *this;
  }

  optinal<T> &operator=(T &val) {
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
