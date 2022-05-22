#pragma once

#include <csignal>
#include <cstdint>

#include "util/atomic_bitmap.h"

namespace core {

class SignalFlag final {
 public:
  static SignalFlag &Get() {
    static SignalFlag instance;
    return instance;
  }

  bool Has(int signal) const {
    return bitmap_.Test(signal);
  }

  void Set(int signal) {
    bitmap_.Set(signal);
  }

  void Reset(int signal) {
    bitmap_.Reset(signal);
  }

 private:
  util::AtomicBitmap<uint64_t> bitmap_;
};

}  // namespace core
