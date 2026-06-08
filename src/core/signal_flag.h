#pragma once

#include <cassert>
#include <csignal>
#include <cstdint>

#include "util/atomic_bitmap.h"

namespace core {

// Process-wide latch that the signal handler writes into and the main
// loop polls. The handler must be async-signal-safe, which rules out
// LOG_* (they touch stderr / files): we use only an atomic bitmap
// store and `assert` (a `((void)0)` no-op under NDEBUG).
//
// Bit layout: each signal occupies one dedicated bit at position
// `signal`, mapped via `1ULL << signal`. We deliberately do NOT use
// the raw signal number as a bitmask -- POSIX signal numbers are
// densely packed integers (SIGINT=2, SIGUSR1=10, SIGTERM=15, ...)
// whose set bits overlap, so a "mask of bits I care about" approach
// would let Reset(one_signal) silently clear another signal's pending
// bit. Using `1<<n` gives every signal its own bit unconditionally.
//
// The `signal < 64` precondition is asserted on every entry point.
// All standard POSIX signal numbers fit comfortably (SIGRTMAX on
// Linux is typically 64 and the runtime only registers a fixed
// subset of low-numbered signals; see RegisterSignalHandlers in
// main.cc). If a future caller starts passing higher-numbered
// real-time signals, the bitmap width must grow first.
class SignalFlag final {
 public:
  static SignalFlag &Get() {
    static SignalFlag instance;
    return instance;
  }

  bool Has(int signal) const {
    assert(signal >= 0 && signal < 64);
    return bitmap_.Test(MaskOf(signal));
  }

  void Set(int signal) {
    assert(signal >= 0 && signal < 64);
    bitmap_.Set(MaskOf(signal));
  }

  void Reset(int signal) {
    assert(signal >= 0 && signal < 64);
    bitmap_.Reset(MaskOf(signal));
  }

 private:
  static constexpr uint64_t MaskOf(int signal) {
    // `1ULL << signal` for any 0 <= signal < 64. UB for negative or
    // >=64 inputs, hence the precondition assert at every call site.
    return uint64_t{1} << signal;
  }

  util::AtomicBitmap<uint64_t> bitmap_;
};

}  // namespace core
