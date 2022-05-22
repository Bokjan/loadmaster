#pragma once

#include <atomic>

namespace util {

template <typename T>
class AtomicBitmap {
 public:
  AtomicBitmap() { Clear(); }
  bool Test(T bit) const { return (atomic_.load() & bit) == bit; }
  void Set(T bit) { atomic_.fetch_or(bit); }
  void Reset(T bit) { atomic_.fetch_and(~bit); }
  void Clear() { atomic_.store(static_cast<T>(0)); }

 private:
  std::atomic<T> atomic_;
};

}  // namespace util
