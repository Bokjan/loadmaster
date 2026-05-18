#pragma once

#include <numeric>
#include <vector>

namespace util {

// Fixed-size ring buffer that maintains a rolling arithmetic mean.
//
// Properties:
//   * Capacity is fixed at construction.
//   * Mean is maintained incrementally in O(1) per insert (instead of O(N))
//     by tracking a running sum.
//   * Both the warming-up phase (before the buffer is full) and the
//     steady-state ring rotation are handled correctly.
template <typename T>
class RollingSampler final {
 public:
  explicit RollingSampler(int size)
      : capacity_(size), index_(0), sum_(static_cast<T>(0)), mean_cached_(static_cast<T>(0)) {
    values_.reserve(capacity_);
  }

  T GetMean() const { return mean_cached_; }
  int GetSampleCount() const { return static_cast<int>(values_.size()); }

  void InsertValue(const T &value) {
    if (static_cast<int>(values_.size()) < capacity_) {
      values_.emplace_back(value);
      sum_ += value;
    } else {
      sum_ -= values_[index_];
      sum_ += value;
      values_[index_] = value;
      ++index_;
      if (index_ >= capacity_) {
        index_ = 0;
      }
    }
    mean_cached_ = sum_ / static_cast<T>(values_.size());
  }

 private:
  int capacity_;
  int index_;
  std::vector<T> values_;
  T sum_;
  T mean_cached_;
};

}  // namespace util
