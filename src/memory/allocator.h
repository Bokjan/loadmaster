#pragma once

#include <cstddef>
#include <stop_token>

namespace memory::unsafe {

class Allocator final {
 public:
  Allocator();
  ~Allocator();
  Allocator(Allocator &&);
  Allocator(const Allocator &) = delete;
  void AllocateBlock(size_t target_size);
  void ReleaseBlock();
  void FillXor(std::byte seed);
  // Same as FillXor but yields between fixed-size chunks and returns early
  // once `st` is stop-requested, so a long fill on a background jthread can
  // be interrupted promptly during shutdown. The byte-level effect is
  // identical to FillXor for the prefix that was actually filled; on early
  // return the tail is left untouched (acceptable -- the block is about to
  // be released anyway).
  void FillXorInterruptible(std::byte seed, std::stop_token st);
  bool IsEmpty() const { return size_ == 0 && block_ptr_ == nullptr; }

 private:
  size_t size_;
  std::byte *block_ptr_;
  void ResetFields();
};

}  // namespace memory::unsafe
