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
  void FillXor(std::byte seed);
  bool IsEmpty() const { return size_ == 0 && block_ptr_ == nullptr; }

 private:
  size_t size_;
  std::byte *block_ptr_;
  void ResetFields();
};

}  // namespace memory::unsafe
