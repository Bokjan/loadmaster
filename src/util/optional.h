#pragma once

#include <utility>

namespace util {

template <typename T>
class Optional final {
 public:
  Optional() = default;
  Optional(const Optional &) = default;
  Optional &operator=(const Optional &) = default;
  Optional(Optional &&) = default;
  Optional &operator=(Optional &&) = default;
  ~Optional() = default;

  template <typename... Args>
  explicit Optional(Args &&...args) : value_(true, T(std::forward<Args>(args)...)) {}

  Optional<T> &operator=(T &&val) {
    value_.first = true;
    value_.second = val;
    return *this;
  }

  Optional<T> &operator=(T &val) {
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
