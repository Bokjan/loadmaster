#include "manager_default.h"

#include <new>

#include "constants.h"

#include "core/constants.h"
#include "core/options.h"
#include "util/log.h"

namespace memory {

MemoryResourceManagerDefault::MemoryResourceManagerDefault(const core::Options &options)
    : MemoryResourceManager(options), generator_(std::random_device{}()) {}

MemoryResourceManagerDefault::~MemoryResourceManagerDefault() {
  // If a background allocator thread is still running, signal it to stop
  // and wait. `std::jthread` already does this in its destructor, but doing
  // it explicitly makes the lifetime obvious.
  if (bg_alloc_thread_.joinable()) {
    bg_alloc_thread_.request_stop();
    bg_alloc_thread_.join();
  }
}

void MemoryResourceManagerDefault::Schedule(TimePoint time_point) {
  if (!WillSchedule(time_point)) {
    return;
  }
  // Skip scheduling if a previous background fill is still in flight.
  if (bg_alloc_thread_.joinable()) {
    LOG_TRACE("background allocate-and-fill still running, skipping this tick");
    return;
  }

  // Compute new block size, aligned to 4 KiB.
  std::uniform_real_distribution<> ratio_dis(kMemoryMinimumRatio, 1.0);
  const double ratio = ratio_dis(generator_);
  size_t byte_count = static_cast<size_t>(ratio * options_.GetMemoryBytes());
  constexpr size_t kFourKiB = 4 * kKibiByte;
  byte_count = byte_count / kFourKiB * kFourKiB;
  LOG_TRACE("byte_count=%zu", byte_count);

  std::uniform_int_distribution<int> byte_dis(0, 255);
  const auto seed = static_cast<std::byte>(byte_dis(generator_));

  const size_t threshold_bytes = static_cast<size_t>(kMemoryNoThreadSpawnThresholdMiB) * kMebiByte;
  if (byte_count >= threshold_bytes) {
    // Spawn a one-shot background thread to allocate + fill the block,
    // so the main scheduling loop is not blocked on a potentially long
    // memset/page-fault storm.
    bg_alloc_thread_ = std::jthread(
        [this, byte_count, seed](std::stop_token) { AllocateAndFill(byte_count, seed); });
  } else {
    AllocateAndFill(byte_count, seed);
  }

  last_scheduling_ = time_point;
}

bool MemoryResourceManagerDefault::WillSchedule(TimePoint time_point) {
  if (allocator_.IsEmpty()) {
    LOG_INFO("memory block is empty, will allocate");
    return true;
  }
  const auto time_diff =
      std::chrono::duration_cast<std::chrono::seconds>(time_point - last_scheduling_);
  return time_diff.count() > kMemoryScheduleIntervalSecond;
}

void MemoryResourceManagerDefault::AllocateAndFill(size_t byte_count, std::byte seed) {
  try {
    allocator_.AllocateBlock(byte_count);
    allocator_.FillXor(seed);
  } catch (const std::bad_alloc &e) {
    LOG_ERROR("memory allocation of %zu bytes failed: %s", byte_count, e.what());
  } catch (const std::exception &e) {
    LOG_ERROR("memory allocation threw: %s", e.what());
  }
}

}  // namespace memory
