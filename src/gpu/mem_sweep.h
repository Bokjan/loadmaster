#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gpu {

// Per-launch arguments for the busy kernel's pass over the `-gm` block.
// Every backend's kernel takes the same five values after (out, loop_count):
//
//   mem         segment `segment` of the block, as allocated
//   mem_offset  first word of the window in that segment (64-bit)
//   mem_words   window length in 32-bit words; 0 = no memory traffic
//   mem_base    word of the window that thread 0 touches first, in
//               [0, mem_words)
//   mem_stride  ALU iterations between two accesses (>= 1)
//
// Each thread touches mem[mem_offset + (mem_base + tid + k * threads) %
// mem_words] after every mem_stride iterations, so a warp's accesses are
// coalesced and one launch covers a contiguous, wrapping run of the window.
struct SweepArgs {
  std::size_t segment = 0;
  std::uint64_t offset_words = 0;
  std::uint32_t words = 0;
  std::uint32_t base = 0;
  int stride = 1;
};

// Walks the `-gm` block across launches so its traffic reaches all of it
// (and not just a part that stays in the GPU's L2) instead of hammering
// the first few MiB. Backends allocate the block in one or more segments;
// the sweep splits each into windows the kernel can index with 32-bit
// word offsets and visits them in turn. Not thread-safe; owned by the
// device, which is only called from one thread at a time.
class MemSweep {
 public:
  // Lay the windows out over the segments of a freshly allocated block.
  void Reset(const std::vector<std::size_t> &segment_bytes);
  void Clear();

  // Arguments for a launch of `loop_count` iterations per thread over
  // `threads` threads in all (the launch's real shape) that should read
  // and write back about `traffic_bytes`; advances the cursor past the
  // words it covers. Every thread touches the same number of words, so a
  // launch moves a multiple of `threads` accesses: what that leaves over
  // is carried to the next launch, which keeps the long-run rate exact
  // even when a launch asks for less than one access per thread. Without
  // a block or traffic, words = 0 and a single chunk of loop_count
  // iterations. Windows smaller than `threads` words (the kernel's index
  // wrap needs at least one word per thread) are skipped.
  SweepArgs Next(int loop_count, std::uint64_t traffic_bytes, std::uint32_t threads);

 private:
  struct Window {
    std::size_t segment;
    std::uint64_t offset_words;
    std::uint32_t words;
  };
  std::vector<Window> windows_;
  std::size_t window_ = 0;
  std::uint32_t cursor_ = 0;
  // Traffic asked for but not delivered yet, in bytes.
  std::uint64_t pending_bytes_ = 0;
};

}  // namespace gpu
