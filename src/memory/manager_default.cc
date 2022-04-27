#include "manager_default.h"

#include "constants.h"
#include "core/options.h"
#include "util/log.h"

namespace memory {

MemoryResourceManagerDefault::MemoryResourceManagerDefault(const core::Options &options)
    : MemoryResourceManager(options), generator_(std::random_device{}()) {}

MemoryResourceManagerDefault::~MemoryResourceManagerDefault() {
  // dtor is invoked after wg.Done(), no race conditions
}

void MemoryResourceManagerDefault::Schedule(TimePoint time_point) {
  do {
    if (!this->WillSchedule(time_point)) {
      break;
    }
    // New size (byte)
    std::uniform_real_distribution<> dis(kMemoryMinimumRatio, 1.0);
    auto ratio = dis(generator_);
    auto byte_count = static_cast<size_t>(ratio * options_.GetMemoryMiB());
    // Align to 4 KiB
    constexpr size_t kFourKiB = 4 * kKibiByte;
    byte_count = byte_count / kFourKiB * kFourKiB;
    LOG_TRACE("byte_count=%lu", byte_count);
    // Allocate and fill
    allocator_.AllocateBlock(byte_count);
    std::uniform_int_distribution<> dis_byte(0, 255);
    allocator_.FillXor(static_cast<std::byte>(dis_byte(generator_)));
    // Update last schduling time
    this->SetLastScheduling(time_point);
  } while (false);
}

bool MemoryResourceManagerDefault::WillSchedule(TimePoint time_point) {
  if (allocator_.IsEmpty()) {
    LOG_INFO("memory block is nullptr");
    return true;
  }
  auto time_diff =
      std::chrono::duration_cast<std::chrono::seconds>(time_point - this->GetLastScheduling());
  if (time_diff.count() > kMemoryScheduleIntervalSecond) {
    return true;
  }
  return false;
}

}  // namespace memory
