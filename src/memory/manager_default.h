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

 protected:
  void Schedule(TimePoint time_point) override;

 private:
  std::mt19937 generator_;
  unsafe::Allocator allocator_;
  // One-shot background thread used when the block size is at least
  // kMemoryBackgroundThreadThresholdMiB, so a long allocate+fill does not
  // stall the 100 ms scheduling loop.
  std::jthread bg_alloc_thread_;
  // True while bg_alloc_thread_ is executing an allocate-and-fill. We can
  // NOT use bg_alloc_thread_.joinable() for this: a jthread stays joinable
  // after its function returns, until it is joined/reassigned/destroyed, so
  // joinable() would be latched true forever and every later tick would
  // bail out -- killing the 45 s periodic resize (see H1).
  std::atomic<bool> bg_in_flight_{false};
  TimePoint last_scheduling_;

  bool WillSchedule(TimePoint time_point);
  // Perform allocate + fill. May be invoked inline or from bg_alloc_thread_;
  // `st` is the background thread's stop token (a default-constructed token
  // for the inline path, which never reports stop-requested). Catches
  // std::bad_alloc so a failed allocation doesn't take the process down.
  void AllocateAndFill(size_t byte_count, std::byte seed, std::stop_token st);
};

}  // namespace memory
