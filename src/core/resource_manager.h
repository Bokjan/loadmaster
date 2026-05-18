#pragma once

#include <chrono>

#include "options.h"

using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

namespace core {

class ResourceManager {
 public:
  explicit ResourceManager(const Options &options) : options_(options) {}
  virtual ~ResourceManager() = default;
  virtual bool Init() = 0;
  virtual void CreateWorkerThreads() = 0;
  virtual void RequestWorkerThreadsStop() = 0;
  virtual void JoinWorkerThreads() = 0;
  virtual void Schedule(TimePoint time_point) = 0;

 protected:
  const Options &options_;

  TimePoint GetLastScheduling() const { return last_scheduling_; }
  void SetLastScheduling(TimePoint tp) { last_scheduling_ = tp; }

 private:
  TimePoint last_scheduling_;
};

}  // namespace core
