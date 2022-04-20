#pragma once

#include <cstdint>

#include "core/resource_manager.h"

namespace memory {

class MemoryResourceManager : public ResourceManager {
 public:
  virtual bool Init();
  virtual void CreateWorkerThreads() override final;

 protected:
  explicit MemoryResourceManager(const Options &options);

  TimePoint GetLastScheduling() const { return last_scheduling_; }
  void SetLastScheduling(TimePoint time_point) { last_scheduling_ = time_point; }

 private:
  TimePoint last_scheduling_;
};

}  // namespace memory
