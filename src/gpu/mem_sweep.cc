#include "mem_sweep.h"

#include <algorithm>

#include "constants.h"

namespace gpu {

void MemSweep::Reset(const std::vector<std::size_t> &segment_bytes) {
  Clear();
  for (std::size_t seg = 0; seg < segment_bytes.size(); ++seg) {
    for (std::size_t offset = 0; offset < segment_bytes[seg];
         offset += kGpuMemSweepWindowMaxBytes) {
      const std::size_t bytes = std::min(segment_bytes[seg] - offset, kGpuMemSweepWindowMaxBytes);
      const auto words = static_cast<std::uint32_t>(bytes / sizeof(std::uint32_t));
      if (words > 0) {
        windows_.push_back({seg, offset / sizeof(std::uint32_t), words});
      }
    }
  }
}

void MemSweep::Clear() {
  windows_.clear();
  window_ = 0;
  cursor_ = 0;
  pending_bytes_ = 0;
}

SweepArgs MemSweep::Next(int loop_count, std::uint64_t traffic_bytes, std::uint32_t threads) {
  SweepArgs args;
  args.stride = std::max(loop_count, 1);
  if (windows_.empty() || threads == 0) {
    return args;
  }
  // The current window, or the next one that can take the launch.
  std::size_t tries = 0;
  while (windows_[window_].words < threads) {
    if (++tries == windows_.size()) {
      return args;  // none can
    }
    window_ = (window_ + 1) % windows_.size();
    cursor_ = 0;
  }
  pending_bytes_ += traffic_bytes;
  const std::uint64_t per_thread =
      pending_bytes_ / kGpuMemTrafficBytesPerAccess / std::uint64_t{threads};
  if (per_thread == 0) {
    return args;  // less than one access per thread so far: carried over
  }
  const Window &w = windows_[window_];
  args.segment = w.segment;
  args.offset_words = w.offset_words;
  args.words = w.words;
  args.base = cursor_;
  // Spread the accesses evenly over the launch, at most one per iteration.
  const auto loops = static_cast<std::uint64_t>(args.stride);
  const std::uint64_t accesses = std::min(per_thread, loops);
  const std::uint64_t stride = (loops + accesses - 1) / accesses;
  args.stride = static_cast<int>(stride);
  // Every thread touches one word after each full or partial chunk.
  const std::uint64_t touched = (loops + stride - 1) / stride * threads;
  // Keep less than one access per thread for the next launch: a shortfall
  // beyond that (more traffic than the iterations allow) is dropped rather
  // than piled up.
  pending_bytes_ -= std::min(pending_bytes_, touched * kGpuMemTrafficBytesPerAccess);
  pending_bytes_ = std::min<std::uint64_t>(
      pending_bytes_, std::uint64_t{threads} * kGpuMemTrafficBytesPerAccess - 1);
  if (touched >= std::uint64_t{w.words} - cursor_) {
    // This launch reaches the end of the window: the next starts afresh on
    // the following one.
    window_ = (window_ + 1) % windows_.size();
    cursor_ = 0;
  } else {
    cursor_ += static_cast<std::uint32_t>(touched);
  }
  return args;
}

}  // namespace gpu
