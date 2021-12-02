#pragma once

#include <atomic>

namespace util {

template <typename T>
class WaitGroup {
 public:
  WaitGroup() { atomic_.store(0); }
  void Add(T value) { atomic_.fetch_add(value); }
  void Incr() { Add(static_cast<T>(1)); }
  void Done() { atomic_.fetch_sub(static_cast<T>(1)); }
  void Wait() const {
    while (atomic_.load() != static_cast<T>(0)) {
      continue;
    }
  }
  T NativeValue() const { return atomic_.load(); }

 private:
  std::atomic<T> atomic_;
};

}  // namespace util
