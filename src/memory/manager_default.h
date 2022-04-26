#pragma once

#include <random>

#include "memory/manager.h"
#include "memory/allocator.h"

namespace memory {

class MemoryResourceManagerDefault final : public MemoryResourceManager {
 public:
  explicit MemoryResourceManagerDefault(const core::Options &options);
  ~MemoryResourceManagerDefault();

 protected:
  virtual void Schedule(TimePoint time_point) override;

 private:
  std::mt19937 generator_;
  unsafe::Allocator allocator_;
  bool WillSchedule(TimePoint time_point);
};

}  // namespace memory
