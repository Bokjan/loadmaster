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
  size_ = target_size;
  block_ptr_ = new std::byte[size_];
}

void Allocator::FillRandomly(std::byte seed) {
  if (block_ptr_ == nullptr) {
    return;
  }
  for (size_t i = 0; i < size_; ++i) {
    block_ptr_[i] ^= seed;
  }
}

void Allocator::ResetFields() {
  size_ = 0;
  block_ptr_ = nullptr;
}

}  // namespace memory::unsafe
