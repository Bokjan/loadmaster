#pragma once

#include "resmgr.h"

#include <cstdint>

#include <vector>
#include <random>

namespace memory {

class MemoryResourceManager : public ResourceManager {
 public:
  virtual bool Init();
  virtual void CreateWorkerThreads();

 protected:
  TimePoint time_point_;
  uint64_t *block_ptr_;
  explicit MemoryResourceManager(const Options &options);
};

class MemoryResourceManagerSimple final : public MemoryResourceManager {
 public:
  explicit MemoryResourceManagerSimple(const Options &options);
  ~MemoryResourceManagerSimple();

 protected:
  virtual void Schedule(TimePoint time_point);

 private:
  std::random_device rd_;
  bool WillSchedule(TimePoint time_point);
};

}  // namespace memory
