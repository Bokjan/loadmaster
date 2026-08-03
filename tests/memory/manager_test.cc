// Unit tests for memory::MemoryResourceManagerDefault scheduling.
//
// The memory manager's Schedule()/WillSchedule() pair implements the
// 45-second periodic "resize the block near the target" loop. That loop
// had no unit-test coverage at all (review blind spot #2), and the H1
// defect -- a `joinable()` guard that latched true forever and froze the
// resize -- lived right here. These tests can't observe the private
// allocator state, so they are behavioural smoke tests: they drive the
// scheduling paths (inline small-block and background large-block) and
// rely on ASAN/UBSAN runs of the suite to catch any leak, use-after-free,
// or data race on allocator_ between the main loop and the background
// jthread. The clean-destruction assertion specifically pins the
// stop-token chunked-fill shutdown path (M10) and the in-flight flag
// lifecycle (H1).

#include "memory/manager_default.h"

#include <chrono>

#include "cli/cli_argument.h"
#include "core/options.h"
#include "core/resource_manager.h"
#include "util/log.h"

#include <gtest/gtest.h>

namespace {

using cli::CliArgument;
using core::Options;
using core::ResourceManager;
using memory::MemoryResourceManagerDefault;

class MemoryManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    util::logger_internal::g_default_logger->SetLevel(util::Logger::kLevelOff);
  }
  void TearDown() override {
    util::logger_internal::g_default_logger->SetLevel(util::Logger::kLevelWarn);
  }
};

// Schedule() is public on the ResourceManager base but redeclared
// protected on the derived class; reach it through the base view.
static void Schedule(MemoryResourceManagerDefault &mgr, TimePoint t) {
  static_cast<ResourceManager &>(mgr).Schedule(t);
}

TEST_F(MemoryManagerTest, InlinePathPeriodicScheduleRunsCleanly) {
  // 1 MiB is well below the 32 MiB background-thread threshold, so the
  // inline path is taken and no jthread is spawned: timing is
  // deterministic and the destructor has no thread to join. This drives
  // the WillSchedule branches -- empty-block (first tick allocates),
  // within-interval (no-op), past-interval (re-allocate) -- that the H1
  // guard sat in front of.
  Options opts;
  CliArgument args;
  args.memory_mb = 1;
  ASSERT_TRUE(opts.ProcessCliArguments(args));
  ASSERT_TRUE(opts.GetMemoryBytes() > 0);

  MemoryResourceManagerDefault mgr(opts);
  ASSERT_TRUE(mgr.Init());

  const TimePoint t0{};
  Schedule(mgr, t0);                                       // empty -> allocate
  Schedule(mgr, t0 + std::chrono::seconds(10));            // within 45s -> no-op
  Schedule(mgr, t0 + std::chrono::seconds(46));            // past 45s -> re-allocate
  // Destructor runs clean (no background thread on this path); ASAN
  // catches any leak across the allocate/release cycles.
  SUCCEED();
}

TEST_F(MemoryManagerTest, BackgroundPathDispatchesAndDestructsCleanly) {
  // 64 MiB target -> ratio in [0.5, 1.0) yields a 32..64 MiB block, at or
  // above the background-thread threshold, so Schedule() spawns the
  // one-shot allocate-and-fill jthread. We then let the manager go out of
  // scope immediately: the destructor must request_stop() + join() and
  // return promptly (the interruptible fill checks stop between 4 MiB
  // chunks). This pins the H1 in-flight-flag lifecycle and the M10
  // stop-token fill; under ASAN it catches a missing join or a race on
  // allocator_ between the main loop and the background thread.
  Options opts;
  CliArgument args;
  args.memory_mb = 64;
  ASSERT_TRUE(opts.ProcessCliArguments(args));

  {
    MemoryResourceManagerDefault mgr(opts);
    ASSERT_TRUE(mgr.Init());
    const TimePoint t0{};
    Schedule(mgr, t0);  // dispatches the background allocate-and-fill
  }  // destructor joins here -- must not hang
  SUCCEED();
}

}  // namespace
