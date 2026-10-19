#pragma once

#include <atomic>

namespace util {

template <typename T>
class AtomicBitmap {
 public:
  // constexpr so an AtomicBitmap with static storage duration is
  // constant-initialized (no dynamic-init guard): see core::SignalFlag,
  // which is written from a signal handler.
  constexpr AtomicBitmap() : atomic_(static_cast<T>(0)) {}
  bool Test(T bit) const { return (atomic_.load() & bit) == bit; }
  void Set(T bit) { atomic_.fetch_or(bit); }
  void Reset(T bit) { atomic_.fetch_and(~bit); }
  void Clear() { atomic_.store(static_cast<T>(0)); }

 private:
  std::atomic<T> atomic_;
};

}  // namespace util
