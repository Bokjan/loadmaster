#pragma once

#include "resmgr.h"

#include <cstdint>

#include <random>
#include <vector>

namespace memory {

class MemoryResourceManager : public ResourceManager {
 public:
  virtual bool Init();
  virtual void CreateWorkerThreads();

 protected:
  TimePoint time_point_;
  uint64_t *block_ptr_;  // TODO: solve concurrently write
  explicit MemoryResourceManager(const Options &options);
};

class MemoryResourceManagerDefault final : public MemoryResourceManager {
 public:
  explicit MemoryResourceManagerDefault(const Options &options);
  ~MemoryResourceManagerDefault();

 protected:
  virtual void Schedule(TimePoint time_point);

 private:
  std::random_device rd_;
  bool WillSchedule(TimePoint time_point);
};

}  // namespace memory
