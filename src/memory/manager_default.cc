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
  // it explicitly makes the lifetime obvious. The fill loop checks the stop
  // token between chunks, so this join returns promptly even mid-fill.
  if (bg_alloc_thread_.joinable()) {
    bg_alloc_thread_.request_stop();
    bg_alloc_thread_.join();
  }
}

void MemoryResourceManagerDefault::Schedule(TimePoint time_point) {
  // Check the in-flight flag BEFORE WillSchedule. While a background fill is
  // running, allocator_ is being mutated by that thread; observing
  // bg_in_flight_==false (acquire) guarantees the background thread's
  // allocator_ writes are complete (they precede its release-store of false),
  // so WillSchedule's IsEmpty() read is race-free. Checking the flag first
  // also means we never read allocator_ concurrently with the fill.
  if (bg_in_flight_.load(std::memory_order_acquire)) {
    LOG_TRACE("background allocate-and-fill still running, skipping this tick");
    return;
  }
  if (!WillSchedule(time_point)) {
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

  const size_t threshold_bytes =
      static_cast<size_t>(kMemoryBackgroundThreadThresholdMiB) * kMebiByte;
  if (byte_count >= threshold_bytes) {
    // Spawn a one-shot background thread to allocate + fill the block, so
    // the main scheduling loop is not blocked on a potentially long
    // memset/page-fault storm. Reassigning bg_alloc_thread_ joins the
    // previous (already-finished) thread first; we only reach here when not
    // in flight, so that previous thread has already cleared the flag.
    bg_in_flight_.store(true, std::memory_order_release);
    bg_alloc_thread_ = std::jthread([this, byte_count, seed](std::stop_token st) {
      AllocateAndFill(byte_count, seed, st);
      bg_in_flight_.store(false, std::memory_order_release);
    });
  } else {
    AllocateAndFill(byte_count, seed, std::stop_token{});
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

void MemoryResourceManagerDefault::AllocateAndFill(size_t byte_count, std::byte seed,
                                                   std::stop_token st) {
  try {
    allocator_.AllocateBlock(byte_count);
    allocator_.FillXorInterruptible(seed, st);
  } catch (const std::bad_alloc &e) {
    LOG_ERROR("memory allocation of %zu bytes failed: %s", byte_count, e.what());
  } catch (const std::exception &e) {
    LOG_ERROR("memory allocation threw: %s", e.what());
  }
}

}  // namespace memory
