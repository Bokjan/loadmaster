// Unit tests for core::SignalFlag.
//
// SignalFlag is a process-wide singleton that wraps a
// util::AtomicBitmap<uint64_t>. Its sole purpose is to give the
// signal handler an async-signal-safe way to record which signals
// have been observed, so the main loop can pick them up on its next
// pass without taking a lock. The class is intentionally tiny --
// every public method is one line of forwarding to AtomicBitmap
// after mapping the signal number to its dedicated bit `1ULL << n`.
//
// What we pin here:
//
//   * Get() returns the same instance on every call (singleton
//     contract; the runtime relies on the signal handler and the
//     main loop seeing the same bitmap).
//   * State survives across Get() calls within a test (the
//     instance is a function-local static, not a per-call object).
//   * Each signal occupies its OWN bit (`1ULL << signal`), so that
//     Set / Reset / Has for one signal cannot interfere with the
//     pending state of another -- even when the two signal
//     numbers' raw integer values share set bits (e.g. SIGINT == 2
//     and SIGTERM == 15 overlap on bit 0x2). This bit-disjointness
//     is what makes Runtime::DealSignals() correct when multiple
//     signals land in a single scheduler tick.
//
// AtomicBitmap's underlying Set / Reset / Test behaviour is covered
// by util/atomic_bitmap_test.cc; we don't re-verify it here.
//
// Because the singleton's state leaks across tests, the fixture
// clears every signal the suite touches in SetUp / TearDown. The
// list is kept small and explicit so it can't trample on other
// consumers of the singleton (none today, but cheap to preserve).

#include "core/signal_flag.h"

#include <csignal>

#include <gtest/gtest.h>

namespace {

using core::SignalFlag;

// Signals this test suite touches. Listed explicitly so SetUp /
// TearDown can scrub exactly these bits without disturbing any
// other consumer of the singleton.
constexpr int kSignalsUnderTest[] = {
    SIGINT,
    SIGTERM,
#ifdef SIGUSR1
    SIGUSR1,
#endif
#ifdef SIGUSR2
    SIGUSR2,
#endif
};

class SignalFlagTest : public ::testing::Test {
 protected:
  void SetUp() override { ClearAll(); }
  void TearDown() override { ClearAll(); }

 private:
  static void ClearAll() {
    SignalFlag &flag = SignalFlag::Get();
    for (int sig : kSignalsUnderTest) {
      flag.Reset(sig);
    }
  }
};

TEST_F(SignalFlagTest, GetReturnsSameInstance) {
  // Singleton contract: the address of the instance is stable for
  // the lifetime of the process. The signal handler captures it at
  // installation time; the main loop reads it on every iteration.
  SignalFlag &a = SignalFlag::Get();
  SignalFlag &b = SignalFlag::Get();
  EXPECT_EQ(&a, &b);
}

TEST_F(SignalFlagTest, FreshFlagHasNoSignalsAfterReset) {
  // SetUp has just cleared every signal we care about; the bitmap
  // must read as "nothing pending" for each of them.
  SignalFlag &flag = SignalFlag::Get();
  for (int sig : kSignalsUnderTest) {
    EXPECT_FALSE(flag.Has(sig)) << "signal " << sig << " should be clear after SetUp";
  }
}

TEST_F(SignalFlagTest, SetMakesHasReturnTrue) {
  SignalFlag &flag = SignalFlag::Get();
  flag.Set(SIGINT);
  EXPECT_TRUE(flag.Has(SIGINT));
}

TEST_F(SignalFlagTest, ResetClearsTheSignal) {
  SignalFlag &flag = SignalFlag::Get();
  flag.Set(SIGINT);
  ASSERT_TRUE(flag.Has(SIGINT));
  flag.Reset(SIGINT);
  EXPECT_FALSE(flag.Has(SIGINT));
}

TEST_F(SignalFlagTest, SetIsIdempotent) {
  // Real signal handlers may fire repeatedly before the main loop
  // gets to drain the flag; the second / third Set() must not
  // toggle the bit off or otherwise misbehave.
  SignalFlag &flag = SignalFlag::Get();
  flag.Set(SIGTERM);
  flag.Set(SIGTERM);
  flag.Set(SIGTERM);
  EXPECT_TRUE(flag.Has(SIGTERM));
}

TEST_F(SignalFlagTest, DistinctSignalsAreIndependentOnSet) {
  // Setting one signal must not raise any other signal's bit. With
  // `1ULL << n` mapping this is trivial; the test exists to lock
  // the invariant in place against any future refactor.
  SignalFlag &flag = SignalFlag::Get();
  flag.Set(SIGINT);
  EXPECT_TRUE(flag.Has(SIGINT));
  EXPECT_FALSE(flag.Has(SIGTERM)) << "Set(SIGINT) must not raise SIGTERM's bit";

  flag.Set(SIGTERM);
  EXPECT_TRUE(flag.Has(SIGINT)) << "Set(SIGTERM) must not clobber SIGINT's bit";
  EXPECT_TRUE(flag.Has(SIGTERM));
}

TEST_F(SignalFlagTest, ResetOfOneSignalLeavesOthersIntact) {
  // The whole reason a bit-per-signal layout was chosen: handling
  // one signal in Runtime::DealSignals() must not accidentally
  // clear another that was queued at the same time.
  //
  // A prior implementation used the raw signal *number* as an
  // AtomicBitmap mask. That made Reset(SIGINT == 2) compatible with
  // Reset(SIGTERM == 15) only by accident -- SIGTERM's mask
  // 0b01111 contains SIGINT's bit 0b00010, so Reset(SIGTERM) wiped
  // SIGINT's pending state too. The current 1ULL<<n mapping makes
  // every signal occupy a disjoint bit, so this test is what guards
  // against a regression to the old behaviour.
  SignalFlag &flag = SignalFlag::Get();
  flag.Set(SIGINT);
  flag.Set(SIGTERM);
  flag.Reset(SIGINT);
  EXPECT_FALSE(flag.Has(SIGINT));
  EXPECT_TRUE(flag.Has(SIGTERM)) << "Reset(SIGINT) must not touch SIGTERM";
}

TEST_F(SignalFlagTest, ResetOfUnsetSignalDoesNotTouchOthers) {
  // Companion to the previous test: a Reset() for a signal that
  // was never raised must also be a clean no-op for every other
  // signal. Same regression target as above.
  SignalFlag &flag = SignalFlag::Get();
  flag.Set(SIGINT);
  flag.Reset(SIGTERM);  // SIGTERM was never set
  EXPECT_FALSE(flag.Has(SIGTERM));
  EXPECT_TRUE(flag.Has(SIGINT)) << "Reset of an unset signal must not touch SIGINT";
}

TEST_F(SignalFlagTest, StateSurvivesAcrossGetCalls) {
  // The singleton's state must persist between Get() calls -- the
  // signal handler and the main loop are different call sites.
  SignalFlag::Get().Set(SIGINT);
  EXPECT_TRUE(SignalFlag::Get().Has(SIGINT));
}

TEST_F(SignalFlagTest, OverlappingSignalNumbersDoNotInterfere) {
  // Explicit coverage of the exact scenario the old mask-based
  // layout used to mishandle: pick two signals whose integer values
  // overlap on the low bits, drive them through the full
  // Set/Has/Reset cycle, and confirm complete independence.
  //
  // On every supported platform SIGINT == 2 (0b0010) and
  // SIGTERM == 15 (0b1111) -- their integer representations share
  // bit 0x2. With the bit-per-signal mapping the two end up on
  // completely different bits (1<<2 vs 1<<15) and never alias.
  SignalFlag &flag = SignalFlag::Get();

  flag.Set(SIGINT);
  EXPECT_TRUE(flag.Has(SIGINT));
  EXPECT_FALSE(flag.Has(SIGTERM));

  flag.Set(SIGTERM);
  EXPECT_TRUE(flag.Has(SIGINT));
  EXPECT_TRUE(flag.Has(SIGTERM));

  flag.Reset(SIGTERM);
  EXPECT_TRUE(flag.Has(SIGINT)) << "Reset(SIGTERM) must not bleed into SIGINT";
  EXPECT_FALSE(flag.Has(SIGTERM));

  flag.Reset(SIGINT);
  EXPECT_FALSE(flag.Has(SIGINT));
  EXPECT_FALSE(flag.Has(SIGTERM));
}

}  // namespace
