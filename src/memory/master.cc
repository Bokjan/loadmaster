#include "master.h"

#include "options.h"
#include "util/log.h"

namespace memory {

MemoryResourceManager::MemoryResourceManager(const Options &options)
    : ResourceManager(options), block_ptr_(nullptr) {}

bool MemoryResourceManager::Init() { return options_.memory_ > 0; }

void MemoryResourceManager::CreateWorkerThreads() {}

MemoryResourceManagerDefault::MemoryResourceManagerDefault(const Options &options)
    : MemoryResourceManager(options), filling_(false) {}

MemoryResourceManagerDefault::~MemoryResourceManagerDefault() {
  // Delete allocated memory
  // dtor is invoked after wg.Done(), no race conditions
  if (block_ptr_ != nullptr) {
    delete[] block_ptr_;
  }
}

void MemoryResourceManagerDefault::Schedule(TimePoint time_point) {
  do {
    if (!this->WillSchedule(time_point)) {
      break;
    }
    // New size (byte)
    std::mt19937 generator(rd_());
    std::uniform_real_distribution<> dis(kMemoryMinimumRatio, 1.0);
    auto ratio = dis(generator);
    auto byte_count = static_cast<size_t>(ratio * options_.memory_);
    // Align to 4 KiB
    constexpr size_t kFourKiB = 4 * 1024;
    byte_count = byte_count / kFourKiB * kFourKiB;
    LOG_TRACE("byte_count=%lu", byte_count);
    // Filling procedure in lambda form
    auto procedure = [byte_count, this]() {
      WaitGroupDoneGuard guard(wg_);
      filling_ = true;
      auto target = byte_count / sizeof(*(block_ptr_));
      if (target <= 0) {
        LOG_ERROR("invalid target(uint64_t) count %lu", target);
        return;
      }
      if (block_ptr_ != nullptr) {
        delete[] block_ptr_;
      }
      block_ptr_ = new uint64_t[target];
      for (decltype(target) i = 0; i < target; ++i) {
        block_ptr_[i] = i;
      }
      filling_ = false;
    };
    // Need a new thread?
    if (byte_count > kMemoryNoThreadSpawnThresholdMiB * 1024 * 1024) {
      std::thread th(procedure);
      th.detach();
    } else {
      procedure();
    }
    wg_.Incr();
    time_point_ = time_point;
  } while (false);
}

bool MemoryResourceManagerDefault::WillSchedule(TimePoint time_point) {
  if (block_ptr_ == nullptr) {
    LOG_INFO("block_ptr_ is nullptr");
    return true;
  }
  if (filling_) {
    LOG_INFO("previous filling task is still running, skip");
    return false;
  }
  auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(time_point - time_point_);
  if (time_diff.count() > kMemoryScheduleIntervalSecond) {
    return true;
  }
  return false;
}

}  // namespace memory
