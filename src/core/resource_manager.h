#pragma once

#include <chrono>

#include "options.h"

using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

namespace core {

// Abstract base for a resource manager. Each subsystem (CPU, memory, GPU)
// owns one implementation, instantiated from its module's factory.
//
// Lifetime (driven by core::Runtime):
//
//   ctor -> Init() -> CreateWorkerThreads()
//                  -> Schedule() ... Schedule()     [every kScheduleIntervalMS]
//                  -> RequestWorkerThreadsStop() -> JoinWorkerThreads() -> dtor
//
// `Schedule()` is called from the runtime's main loop on every tick. The
// default implementation does nothing, which is appropriate for modules
// whose worker threads need no centralised coordination (e.g. GPU's
// default algorithm where each device's load is fixed for the run).
class ResourceManager {
 public:
  explicit ResourceManager(const Options &options) : options_(options) {}
  virtual ~ResourceManager() = default;
  virtual bool Init() = 0;
  virtual void CreateWorkerThreads() = 0;
  virtual void RequestWorkerThreadsStop() = 0;
  virtual void JoinWorkerThreads() = 0;
  virtual void Schedule(TimePoint /*time_point*/) {}

 protected:
  const Options &options_;
};

}  // namespace core
