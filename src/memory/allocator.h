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
  // Overwrite every byte of the block (committing its pages) with a
  // seed-derived pattern. Works in 4 MiB chunks and returns early once
  // `stop` is requested, so a multi-GiB fill can't hold up shutdown; the
  // rest of the block is then left unwritten. No-op on an empty allocator.
  void Fill(std::byte seed, std::stop_token stop = {});
  bool IsEmpty() const { return size_ == 0 && block_ptr_ == nullptr; }
  size_t Size() const { return size_; }

 private:
  size_t size_;
  std::byte *block_ptr_;
  void ResetFields();
};

}  // namespace memory::unsafe
