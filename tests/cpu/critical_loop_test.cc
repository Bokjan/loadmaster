// Unit tests for cpu::CriticalLoop.
//
// CriticalLoop is the inner busy-work routine each CPU worker hands off
// to. It cycles a thread_local accumulator through `count` modular
// multiplications and returns the result. We deliberately test a
// minimal set of properties:
//
//   * count <= 0 short-circuits to 0 without touching the PRNG. This
//     is the contract worker.cc relies on when probing the loop with
//     zero-sized calibration calls.
//   * count > 0 executes -- i.e. the loop is not optimised away. We
//     assert this indirectly through wall-clock time: a non-trivial
//     count must take measurably non-zero time on any host.
//   * Compiles, runs, and returns without UB across a spread of
//     counts. The PRNG seeding and unsigned wrap-around math are
//     covered by simply not crashing under sanitizers.
//
// We deliberately do NOT assert anything about the returned value's
// distribution or magnitude: the implementation seeds `val` and
// `factor` from std::random_device on a thread_local, so the output
// is non-deterministic across runs and across calls. The point of
// the function is to burn cycles, not to compute anything.
//
// We also do NOT assert that doubling `count` doubles the runtime --
// CI hosts have noisy schedulers and the loop is short, so any
// timing ratio assertion would be flaky.

#include "cpu/critical_loop.h"

#include <chrono>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

TEST(CriticalLoopTest, ZeroCountReturnsZero) {
  // Contract: workers may call with count == 0 during calibration;
  // the function must short-circuit before touching the thread_local
  // PRNG state.
  EXPECT_EQ(cpu::CriticalLoop(0), 0u);
}

TEST(CriticalLoopTest, NegativeCountReturnsZero) {
  // Defensive: signed `count` should still hit the early-return guard.
  EXPECT_EQ(cpu::CriticalLoop(-1), 0u);
  EXPECT_EQ(cpu::CriticalLoop(-1'000'000), 0u);
}

TEST(CriticalLoopTest, RunsWithoutCrashingForVariedCounts) {
  // Smoke test across several magnitudes. We only require the call
  // to return (and, implicitly under sanitizers, to do so without
  // UB or memory issues). Anything we could assert about the return
  // value would be brittle given the thread_local PRNG state.
  for (int count : {1, 10, 100, 10'000, 1'000'000}) {
    volatile std::uint32_t sink = cpu::CriticalLoop(count);
    (void)sink;  // keep the call from being elided by the optimiser
  }
}

TEST(CriticalLoopTest, ActuallyBurnsCyclesForLargeCount) {
  // The function exists to burn CPU. If a future refactor accidentally
  // makes the body dead-code-eliminated (e.g. by dropping the
  // thread_local that creates an observable side effect), this loop
  // would suddenly return in nanoseconds and we'd silently stop
  // generating load.
  //
  // We measure a generously large count and require the call to take
  // at least some small positive duration. The threshold is loose
  // (1 microsecond) so even the fastest modern core, with aggressive
  // multiplication throughput, can't legitimately hit it -- 10M
  // dependent multiplies cannot retire in under a microsecond on any
  // real CPU.
  constexpr int kLargeCount = 10'000'000;
  const auto t0 = std::chrono::steady_clock::now();
  volatile std::uint32_t sink = cpu::CriticalLoop(kLargeCount);
  const auto t1 = std::chrono::steady_clock::now();
  (void)sink;

  const auto elapsed_us =
      std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  EXPECT_GT(elapsed_us, 1) << "CriticalLoop(" << kLargeCount
                           << ") returned in " << elapsed_us
                           << "us -- the multiply chain may have been elided";
}

}  // namespace
