#pragma once

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
  // Optional background thread used when block size >= kMemoryNoThreadSpawnThresholdMiB.
  std::jthread bg_alloc_thread_;

  bool WillSchedule(TimePoint time_point);
  // Perform allocate + fill. May be invoked inline or from `bg_alloc_thread_`.
  // Catches std::bad_alloc so a failed allocation doesn't take the process down.
  void AllocateAndFill(size_t byte_count, std::byte seed);
};

}  // namespace memory
