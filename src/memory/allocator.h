#pragma once

#include <cstddef>

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
  // seed-derived pattern. No-op on an empty allocator.
  void Fill(std::byte seed);
  bool IsEmpty() const { return size_ == 0 && block_ptr_ == nullptr; }
  size_t Size() const { return size_; }

 private:
  size_t size_;
  std::byte *block_ptr_;
  void ResetFields();
};

}  // namespace memory::unsafe
