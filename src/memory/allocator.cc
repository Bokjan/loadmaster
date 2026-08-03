#include "allocator.h"

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
  // Allocate first, then publish the size: if `new` throws std::bad_alloc
  // we must not leave size_ == target_size with block_ptr_ == nullptr, which
  // would violate IsEmpty()'s (size_ == 0 && block_ptr_ == nullptr) contract
  // and confuse WillSchedule's "is the block empty?" heuristic.
  std::byte *p = new std::byte[target_size];
  block_ptr_ = p;
  size_ = target_size;
}

void Allocator::FillXor(std::byte seed) {
  if (block_ptr_ == nullptr) {
    return;
  }
  for (size_t i = 0; i < size_; ++i) {
    block_ptr_[i] ^= seed;
  }
}

void Allocator::FillXorInterruptible(std::byte seed, std::stop_token st) {
  if (block_ptr_ == nullptr) {
    return;
  }
  // Touch the memory in fixed chunks so a multi-hundred-MiB fill (which
  // forces physical page commit via write faults) can be aborted between
  // chunks when the owning jthread is asked to stop.
  constexpr size_t kChunkBytes = 4 * 1024 * 1024;  // 4 MiB
  size_t off = 0;
  while (off < size_) {
    if (st.stop_requested()) {
      return;
    }
    const size_t end = (off + kChunkBytes < size_) ? off + kChunkBytes : size_;
    for (size_t i = off; i < end; ++i) {
      block_ptr_[i] ^= seed;
    }
    off = end;
  }
}

void Allocator::ResetFields() {
  size_ = 0;
  block_ptr_ = nullptr;
}

}  // namespace memory::unsafe
