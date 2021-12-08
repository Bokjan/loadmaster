#pragma once

#include "resmgr.h"

#include <cstdint>

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

}  // namespace memory
