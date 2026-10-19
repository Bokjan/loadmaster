#include "allocator.h"

#include <algorithm>

namespace memory::unsafe {

Allocator::Allocator() : size_(0), block_ptr_(nullptr) {}

Allocator::~Allocator() { ReleaseBlock(); }

Allocator::Allocator(Allocator &&other) : size_(other.size_), block_ptr_(other.block_ptr_) {
  other.ResetFields();
}

void Allocator::ReleaseBlock() {
  if (block_ptr_ != nullptr) {
    delete[] block_ptr_;
  }
  ResetFields();
}

void Allocator::AllocateBlock(size_t target_size) {
  ReleaseBlock();
  // Allocate before touching the fields: if `new` throws, the allocator
  // must stay in the consistent empty state rather than claim a size with
  // a null block.
  block_ptr_ = new std::byte[target_size];
  size_ = target_size;
}

void Allocator::Fill(std::byte seed, std::stop_token stop) {
  if (block_ptr_ == nullptr) {
    return;
  }
  constexpr size_t kChunkBytes = 4 * 1024 * 1024;
  // Write-only: the block comes from `new std::byte[]` and is
  // uninitialized, so the old `block[i] ^= seed` read indeterminate
  // values (UB). Every byte is written, so every page gets committed.
  for (size_t begin = 0; begin < size_; begin += kChunkBytes) {
    if (stop.stop_requested()) {
      return;
    }
    const size_t end = std::min(size_, begin + kChunkBytes);
    for (size_t i = begin; i < end; ++i) {
      block_ptr_[i] = seed ^ static_cast<std::byte>(i);
    }
  }
}

void Allocator::ResetFields() {
  size_ = 0;
  block_ptr_ = nullptr;
}

}  // namespace memory::unsafe
