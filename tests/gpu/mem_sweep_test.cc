// Unit tests for gpu::MemSweep, the per-launch arguments of the busy
// kernel's pass over the `-gm` block. The kernels themselves only run on a
// GPU, so Touched() below replays their access pattern on the host (same
// chunking and index wrap as the PTX / HIP / OpenCL / MSL sources) to check
// that a launch stays inside its window, that successive launches sweep
// the whole block, and that the traffic delivered adds up to what was
// asked for.

#include "gpu/mem_sweep.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gpu/constants.h"

#include <gtest/gtest.h>

namespace {

using gpu::MemSweep;
using gpu::SweepArgs;

constexpr std::size_t kWindow = kGpuMemSweepWindowMaxBytes;

// Word indices one launch touches, over all `threads` threads, in the order
// thread 0..n-1 of each chunk. Mirrors the kernels line by line.
std::vector<std::uint32_t> Touched(const SweepArgs &a, int loop_count, std::uint32_t threads) {
  std::vector<std::uint32_t> out;
  if (a.words == 0) {
    return out;
  }
  const int chunk = a.stride > 0 ? a.stride : loop_count;
  std::vector<std::uint32_t> idx(threads);
  for (std::uint32_t tid = 0; tid < threads; ++tid) {
    idx[tid] = a.base + tid;
    if (idx[tid] >= a.words) idx[tid] -= a.words;
  }
  for (int i = 0; i < loop_count;) {
    i += (loop_count - i < chunk ? loop_count - i : chunk);
    for (std::uint32_t tid = 0; tid < threads; ++tid) {
      out.push_back(idx[tid]);
      idx[tid] += threads;
      if (idx[tid] >= a.words) idx[tid] -= a.words;
    }
  }
  return out;
}

TEST(MemSweepTest, NoTrafficWithoutABlock) {
  MemSweep sweep;
  const SweepArgs a = sweep.Next(1000, 1 << 20, 256);
  EXPECT_EQ(a.words, 0u);
  EXPECT_EQ(a.stride, 1000) << "one chunk: the plain ALU loop";
}

TEST(MemSweepTest, NoTrafficWhenNoneIsAsked) {
  MemSweep sweep;
  sweep.Reset({1 << 20});
  EXPECT_EQ(sweep.Next(1000, 0, 256).words, 0u);
  // Less than one access's worth.
  EXPECT_EQ(sweep.Next(1000, kGpuMemTrafficBytesPerAccess - 1, 256).words, 0u);
}

TEST(MemSweepTest, SpreadsTheAccessesOverTheLaunch) {
  MemSweep sweep;
  sweep.Reset({1 << 20});
  // 10 accesses per thread over 1000 iterations: one every 100.
  const SweepArgs a = sweep.Next(1000, 4 * 10 * kGpuMemTrafficBytesPerAccess, 4);
  EXPECT_EQ(a.words, (1u << 20) / 4);
  EXPECT_EQ(a.offset_words, 0u);
  EXPECT_EQ(a.base, 0u);
  EXPECT_EQ(a.stride, 100);
  EXPECT_EQ(Touched(a, 1000, 4).size(), 40u);
}

TEST(MemSweepTest, CarriesLessThanOneAccessPerThreadOver) {
  MemSweep sweep;
  sweep.Reset({1 << 20});
  // One access per launch for 4 threads: nothing until 4 have built up,
  // then one each, at the end of the launch.
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(sweep.Next(1000, kGpuMemTrafficBytesPerAccess, 4).words, 0u) << "launch " << i;
  }
  const SweepArgs a = sweep.Next(1000, kGpuMemTrafficBytesPerAccess, 4);
  EXPECT_NE(a.words, 0u);
  EXPECT_EQ(a.stride, 1000);
  EXPECT_EQ(Touched(a, 1000, 4).size(), 4u);
  EXPECT_EQ(sweep.Next(1000, kGpuMemTrafficBytesPerAccess, 4).words, 0u) << "carry not spent";
}

TEST(MemSweepTest, DeliversWhatWasAskedForOverTime) {
  constexpr std::uint32_t kThreads = 256;
  MemSweep sweep;
  sweep.Reset({64 << 20});
  // Uneven amounts, mostly well under one access per thread, and
  // occasionally several: the delivered total tracks the requested one to
  // within the carry (less than one access per thread).
  std::uint64_t asked = 0;
  std::uint64_t delivered = 0;
  for (int launch = 0; launch < 10'000; ++launch) {
    const std::uint64_t traffic = (launch * 7919u % 997u) * 9u + (launch % 50 == 0 ? 50'000 : 0);
    asked += traffic;
    const SweepArgs a = sweep.Next(100'000, traffic, kThreads);
    delivered += Touched(a, 100'000, kThreads).size() * kGpuMemTrafficBytesPerAccess;
  }
  EXPECT_LE(delivered, asked);
  EXPECT_LT(asked - delivered, std::uint64_t{kThreads} * kGpuMemTrafficBytesPerAccess);
}

TEST(MemSweepTest, ClampsToOneAccessPerIterationAndDropsTheExcess) {
  MemSweep sweep;
  sweep.Reset({1 << 20});
  // Far more than the iterations allow: one per iteration.
  EXPECT_EQ(sweep.Next(10, 1 << 20, 4).stride, 1);
  // What didn't fit isn't carried into later launches.
  EXPECT_EQ(sweep.Next(10, 0, 4).words, 0u);
}

TEST(MemSweepTest, SuccessiveLaunchesContinueWhereTheLastStopped) {
  constexpr std::uint32_t kThreads = 8;
  MemSweep sweep;
  sweep.Reset({4096 * sizeof(std::uint32_t)});
  std::vector<int> hits(4096, 0);
  std::uint32_t expected_base = 0;
  // 7 accesses per thread per launch: 56 words, which doesn't divide 4096,
  // so the last launch before the end wraps around.
  const std::uint64_t traffic = kThreads * 7 * kGpuMemTrafficBytesPerAccess;
  for (int launch = 0; launch < 4096 / 56; ++launch) {
    const SweepArgs a = sweep.Next(700, traffic, kThreads);
    ASSERT_EQ(a.base, expected_base) << "launch " << launch;
    const auto touched = Touched(a, 700, kThreads);
    ASSERT_EQ(touched.size(), 56u);
    for (std::size_t k = 0; k < touched.size(); ++k) {
      // Contiguous run from base: coalesced, nothing skipped or repeated.
      ASSERT_EQ(touched[k], (a.base + k) % a.words);
      ++hits[touched[k]];
    }
    expected_base += 56;
  }
  for (std::size_t w = 0; w < hits.size(); ++w) {
    EXPECT_EQ(hits[w], w < expected_base ? 1 : 0) << "word " << w;
  }
  // The launch that reaches the end of the window starts the next afresh.
  const SweepArgs last = sweep.Next(700, traffic, kThreads);
  EXPECT_EQ(last.base, expected_base);
  EXPECT_EQ(sweep.Next(700, traffic, kThreads).base, 0u);
}

TEST(MemSweepTest, LargeSegmentsAreSplitIntoWindows) {
  MemSweep sweep;
  // 2.5 windows, then a second segment of half a window.
  sweep.Reset({2 * kWindow + kWindow / 2, kWindow / 2});
  struct Expect {
    std::size_t segment, offset, bytes;
  };
  constexpr std::uint64_t kWord = sizeof(std::uint32_t);
  const Expect expected[] = {
      {0, 0, kWindow},     {0, kWindow, kWindow}, {0, 2 * kWindow, kWindow / 2},
      {1, 0, kWindow / 2}, {0, 0, kWindow},  // and round again
  };
  for (const Expect &e : expected) {
    // Enough traffic to reach the end of any window in one launch.
    const SweepArgs a = sweep.Next(1 << 20, std::uint64_t{4} * kWindow, 256);
    EXPECT_EQ(a.segment, e.segment);
    EXPECT_EQ(a.offset_words, e.offset / kWord);
    EXPECT_EQ(a.words, e.bytes / kWord);
    EXPECT_LE(a.words, 1u << 31) << "the kernels' 32-bit index wrap would overflow";
  }
}

TEST(MemSweepTest, WindowsSmallerThanTheGridAreLeftOut) {
  MemSweep sweep;
  // The second segment can't give every thread a word of its own.
  sweep.Reset({1024 * sizeof(std::uint32_t), 100 * sizeof(std::uint32_t)});
  for (int i = 0; i < 3; ++i) {
    const SweepArgs a = sweep.Next(100, std::uint64_t{1} << 30, 256);
    EXPECT_EQ(a.segment, 0u);
    EXPECT_EQ(a.words, 1024u);
  }
  // A smaller launch can use it.
  EXPECT_EQ(sweep.Next(100, std::uint64_t{1} << 30, 64).segment, 1u);
  sweep.Reset({100 * sizeof(std::uint32_t)});
  EXPECT_EQ(sweep.Next(100, std::uint64_t{1} << 30, 256).words, 0u);
}

TEST(MemSweepTest, ClearStopsTheTraffic) {
  MemSweep sweep;
  sweep.Reset({1 << 20});
  sweep.Clear();
  EXPECT_EQ(sweep.Next(1000, 1 << 20, 4).words, 0u);
}

}  // namespace
