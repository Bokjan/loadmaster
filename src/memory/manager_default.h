#pragma once

#include <atomic>
#include <random>
#include <thread>

#include "allocator.h"
#include "manager.h"

namespace memory {

class MemoryResourceManagerDefault final : public MemoryResourceManager {
 public:
  explicit MemoryResourceManagerDefault(const core::Options &options);
  ~MemoryResourceManagerDefault() override;

  // Observers for logging and tests. Main thread only, and only meaningful
  // while no background allocation is in flight.
  size_t BlockBytes() const { return allocator_.Size(); }
  int ScheduledAllocations() const { return scheduled_allocations_; }
  // True from spawning a background allocation until a later Schedule()
  // tick has reaped (joined) it.
  bool AllocationInFlight() const { return bg_alloc_thread_.joinable(); }

 protected:
  void Schedule(TimePoint time_point) override;

 private:
  std::mt19937 generator_;
  // Owned by whichever thread is currently allocating: the main thread
  // when no background fill is in flight, `bg_alloc_thread_` otherwise.
  // The main thread must not touch it while `bg_alloc_thread_` is alive
  // and `bg_alloc_done_` is still false.
  unsafe::Allocator allocator_;
  // Optional background thread used when block size >= kMemoryBackgroundThreadThresholdMiB.
  std::jthread bg_alloc_thread_;
  // Set (release) by `bg_alloc_thread_` as its very last action, so the
  // main thread can tell "finished but not yet joined" apart from "still
  // running" -- std::jthread::joinable() stays true in both cases.
  std::atomic<bool> bg_alloc_done_{false};
  // Whether at least one allocation has been attempted. Before that, the
  // first tick allocates immediately; afterwards every reschedule (also a
  // retry after a failed allocation) waits kMemoryScheduleIntervalSecond.
  bool has_scheduled_ = false;
  int scheduled_allocations_ = 0;
  TimePoint last_scheduling_;

  // Returns true once a background fill has finished (and joins it), or
  // when none was ever started. False while one is still in flight.
  bool ReapBackgroundThread();
  bool WillSchedule(TimePoint time_point) const;
  // Perform allocate + fill. May be invoked inline or from `bg_alloc_thread_`,
  // whose stop token (requested by its destructor) cuts a long fill short.
  // Catches std::bad_alloc so a failed allocation doesn't take the process down.
  void AllocateAndFill(size_t byte_count, std::byte seed, std::stop_token stop = {});
};

}  // namespace memory
