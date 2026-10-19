// Unit tests for memory::MemoryResourceManagerDefault scheduling.
//
// The manager re-sizes its block every kMemoryScheduleIntervalSecond to a
// random size in [kMemoryMinimumRatio, 1] x `-m`, aligned down to 4 KiB.
// Blocks of kMemoryBackgroundThreadThresholdMiB or more are allocated on a
// background thread. A regression we pin here: the background thread was
// detected with jthread::joinable(), which stays true after the thread
// finished, so after the first large block the size never changed again.
//
// Time is injected through Schedule(TimePoint), so "46 seconds later" is
// free; only the background allocation itself takes real time.

#include "memory/manager_default.h"

#include <chrono>
#include <thread>

#include "cli/cli_argument.h"
#include "core/constants.h"
#include "core/options.h"
#include "memory/constants.h"
#include "util/log.h"

#include <gtest/gtest.h>

namespace {

using memory::MemoryResourceManagerDefault;
using std::chrono::seconds;

class MemoryManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    util::logger_internal::g_default_logger->SetLevel(util::Logger::kLevelOff);
  }
  void TearDown() override {
    util::logger_internal::g_default_logger->SetLevel(util::Logger::kLevelWarn);
  }

  static core::Options OptionsWithMiB(int mib) {
    core::Options options;
    cli::CliArgument args;
    args.cpu_load = 0;
    args.memory_mb = mib;
    EXPECT_TRUE(options.ProcessCliArguments(args));
    return options;
  }

  // Drive Schedule() through the public base-class interface.
  static void Tick(MemoryResourceManagerDefault &m, TimePoint t) {
    static_cast<core::ResourceManager &>(m).Schedule(t);
  }

  // Tick at `t` until a pending background allocation has been reaped.
  static void SettleAt(MemoryResourceManagerDefault &m, TimePoint t) {
    for (int i = 0; i < 1000 && m.AllocationInFlight(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      Tick(m, t);
    }
    ASSERT_FALSE(m.AllocationInFlight()) << "background allocation never finished";
  }
};

TEST_F(MemoryManagerTest, FirstTickAllocatesAnAlignedBlockInRange) {
  const auto options = OptionsWithMiB(8);
  MemoryResourceManagerDefault m(options);
  ASSERT_TRUE(m.Init());
  const TimePoint t0{seconds(1000)};
  Tick(m, t0);
  EXPECT_EQ(m.ScheduledAllocations(), 1);
  const size_t bytes = m.BlockBytes();
  EXPECT_EQ(bytes % (4 * kKibiByte), 0u);
  EXPECT_GE(bytes, static_cast<size_t>(8 * kMebiByte * kMemoryMinimumRatio) - 4 * kKibiByte);
  EXPECT_LE(bytes, static_cast<size_t>(8 * kMebiByte));
}

TEST_F(MemoryManagerTest, ReschedulesOnlyAfterTheInterval) {
  const auto options = OptionsWithMiB(8);
  MemoryResourceManagerDefault m(options);
  ASSERT_TRUE(m.Init());
  const TimePoint t0{seconds(1000)};
  Tick(m, t0);
  Tick(m, t0 + seconds(kMemoryScheduleIntervalSecond - 1));
  EXPECT_EQ(m.ScheduledAllocations(), 1);
  Tick(m, t0 + seconds(kMemoryScheduleIntervalSecond + 1));
  EXPECT_EQ(m.ScheduledAllocations(), 2);
}

TEST_F(MemoryManagerTest, KeepsReschedulingAfterABackgroundAllocation) {
  // 64 MiB -> every block is >= 32 MiB -> background thread path.
  const auto options = OptionsWithMiB(64);
  MemoryResourceManagerDefault m(options);
  ASSERT_TRUE(m.Init());
  const TimePoint t0{seconds(1000)};
  Tick(m, t0);
  EXPECT_TRUE(m.AllocationInFlight());
  SettleAt(m, t0);
  ASSERT_EQ(m.ScheduledAllocations(), 1);
  EXPECT_GE(m.BlockBytes(), static_cast<size_t>(32 * kMebiByte));

  const TimePoint t1 = t0 + seconds(kMemoryScheduleIntervalSecond + 1);
  Tick(m, t1);
  EXPECT_EQ(m.ScheduledAllocations(), 2) << "stuck after the first background allocation";
  SettleAt(m, t1);

  const TimePoint t2 = t1 + seconds(kMemoryScheduleIntervalSecond + 1);
  Tick(m, t2);
  EXPECT_EQ(m.ScheduledAllocations(), 3);
  SettleAt(m, t2);
}

TEST_F(MemoryManagerTest, DestroyingMidBackgroundFillJoinsCleanly) {
  // The destructor of the background jthread requests stop and joins; the
  // chunked fill checks the token between 4 MiB chunks, so this returns
  // without waiting for the whole block. Under ASAN it also catches a
  // missing join or a race on the allocator between the two threads.
  const auto options = OptionsWithMiB(64);
  {
    MemoryResourceManagerDefault m(options);
    ASSERT_TRUE(m.Init());
    Tick(m, TimePoint{seconds(1000)});
    EXPECT_TRUE(m.AllocationInFlight());
  }
  SUCCEED();
}

}  // namespace
