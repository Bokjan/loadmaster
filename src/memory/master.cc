#include "master.h"

#include "options.h"
#include "util/log.h"

namespace memory {

MemoryResourceManager::MemoryResourceManager(const Options &options)
    : ResourceManager(options), block_ptr_(nullptr) {}

bool MemoryResourceManager::Init() { return options_.memory_ > 0; }

void MemoryResourceManager::CreateWorkerThreads() {}

MemoryResourceManagerSimple::MemoryResourceManagerSimple(const Options &options)
    : MemoryResourceManager(options) {}

MemoryResourceManagerSimple::~MemoryResourceManagerSimple() {
  // Delete allocated memory
  // dtor is invoked after wg.Done(), no race conditions
  if (block_ptr_ != nullptr) {
    delete[] block_ptr_;
  }
}

void MemoryResourceManagerSimple::Schedule(TimePoint time_point) {
  do {
    if (!this->WillSchedule(time_point)) {
      break;
    }
    // New size (byte)
    std::mt19937 generator(rd_());
    std::uniform_real_distribution<> dis(kMemoryMinimumRatio, 1.0);
    auto ratio = dis(generator);
    auto byte_count = static_cast<size_t>(ratio * options_.memory_);
    // Temporarily spawn a thread for memory alloc & filling
    std::thread th([byte_count, this]() {
      WaitGroupDoneGuard guard(wg_);
      auto target = byte_count / sizeof(*(this->block_ptr_));
      // LOG("target=%ld", target * sizeof(*(this->block_ptr_)));
      if (target <= 0) {
        return;
      }
      if (this->block_ptr_ != nullptr) {
        delete[] this->block_ptr_;
      }
      this->block_ptr_ = new uint64_t[target];
      for (decltype(target) i = 0; i < target; ++i) {
        this->block_ptr_[i] = i;
      }
    });
    wg_.Incr();
    th.detach();
    time_point_ = time_point;
  } while (false);
}

bool MemoryResourceManagerSimple::WillSchedule(TimePoint time_point) {
  if (block_ptr_ == nullptr) {
    return true;
  }
  auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(time_point - time_point_);
  if (time_diff.count() > kMemoryScheduleIntervalSecond) {
    return true;
  }
  return false;
}

}  // namespace memory
