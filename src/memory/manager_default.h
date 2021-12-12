#pragma once

#include "manager.h"

#include <random>

namespace memory {

class MemoryResourceManagerDefault final : public MemoryResourceManager {
 public:
  explicit MemoryResourceManagerDefault(const Options &options);
  ~MemoryResourceManagerDefault();

 protected:
  virtual void Schedule(TimePoint time_point);

 private:
  bool filling_;
  std::mt19937 generator_;
  bool WillSchedule(TimePoint time_point);
};

}  // namespace memory