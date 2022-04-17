#pragma once

#include <vector>

namespace util {

template <typename T>
class RollingSampler final {
 public:
  explicit RollingSampler(int size) : size_(size), index_(0), mean_cached_(static_cast<T>(0)) {
    values_.reserve(size_);
  }

  T GetMean() const { return mean_cached_; }
  int GetSampleCount() const { return static_cast<int>(values_.size()); }

  void InsertValue(const T &value) {
    // Insert to vector
    if (values_.size() < size_) {
      values_.emplace_back(value);
    } else {
      values_[index_] = value;
      index_++;
      if (index_ >= size_) {
        index_ = 0;
      }
    }
    // Pre-calc average value
    T total = 0;
    for (const auto &item : values_) {
      total += item;
    }
    mean_cached_ = total / static_cast<T>(values_.size());
  }

 private:
  int size_;
  int index_;
  std::vector<T> values_;
  T mean_cached_;
};

}  // namespace util
