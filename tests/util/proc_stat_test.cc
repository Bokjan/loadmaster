// Unit tests for util::ProcStat against this test process's real CPU use.
//
// ProcStat turns the per-platform cumulative CPU-time counter of the
// current process into a load figure (100 = one core). We burn one core
// for a while and then idle for a while, and check the reading follows.
// Bounds are deliberately loose -- the counters have jiffy (10 ms)
// resolution on Linux and the test host may be busy -- but they still
// catch unit errors such as the ~42x Mach-tick mix-up fixed on macOS.

#include "util/proc_stat.h"

#include <chrono>
#include <thread>

#include "util/log.h"

#include <gtest/gtest.h>

namespace {

using util::ProcStat;

void BurnFor(std::chrono::milliseconds d) {
  const auto end = std::chrono::steady_clock::now() + d;
  volatile unsigned sink = 1;
  while (std::chrono::steady_clock::now() < end) {
    sink = sink * 1664525u + 1013904223u;
  }
}

TEST(ProcStatTest, StartsAtZero) {
  ProcStat ps;
  EXPECT_EQ(ps.GetCpuLoad(), 0.0);
  ps.UpdateCpuStat(std::chrono::steady_clock::now(), ProcStat::ForceUpdate::kYes);
  EXPECT_EQ(ps.GetCpuLoad(), 0.0) << "the first sample only sets the baseline";
}

TEST(ProcStatTest, BusyThreadReadsAboutOneCore) {
  ProcStat ps;
  ps.UpdateCpuStat(std::chrono::steady_clock::now(), ProcStat::ForceUpdate::kYes);
  BurnFor(std::chrono::milliseconds(600));
  ps.UpdateCpuStat(std::chrono::steady_clock::now(), ProcStat::ForceUpdate::kYes);
  EXPECT_GT(ps.GetCpuLoad(), 60.0);
  EXPECT_LT(ps.GetCpuLoad(), 140.0);
}

TEST(ProcStatTest, IdleThreadReadsNearZero) {
  ProcStat ps;
  ps.UpdateCpuStat(std::chrono::steady_clock::now(), ProcStat::ForceUpdate::kYes);
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  ps.UpdateCpuStat(std::chrono::steady_clock::now(), ProcStat::ForceUpdate::kYes);
  EXPECT_LT(ps.GetCpuLoad(), 25.0);
}

TEST(ProcStatTest, UnforcedUpdatesAreThrottled) {
  ProcStat ps;
  const auto t0 = std::chrono::steady_clock::now();
  ps.UpdateCpuStat(t0, ProcStat::ForceUpdate::kYes);
  BurnFor(std::chrono::milliseconds(200));
  // Within the 500 ms throttle window: no new reading.
  ps.UpdateCpuStat(t0 + std::chrono::milliseconds(100));
  EXPECT_EQ(ps.GetCpuLoad(), 0.0);
}

}  // namespace
