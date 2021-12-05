#pragma once

#include "util/waitgroup.h"

#include <chrono>
#include <functional>
#include <thread>

class Options;
class WorkerContext;
class ResourceManager;
class WorkerLoopGuard;

using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

class WorkerContext {
 public:
  int id_;
  std::thread thread_;

  explicit WorkerContext(int id, util::GenericWaitGroup<int> &wg) : id_(id), parent_wg_(wg) {}
  virtual void Loop() = 0;

 private:
  util::GenericWaitGroup<int> &parent_wg_;
  void OnFinish() { parent_wg_.Done(); }
  friend class WorkerLoopGuard;
};

class WorkerLoopGuard final {
 public:
  WorkerLoopGuard(WorkerContext &ctx);
  ~WorkerLoopGuard();

 private:
  WorkerContext &ctx_;
};

class ResourceManager {
 public:
  explicit ResourceManager(const Options &options) : options_(options) {}
  virtual void InitWithOptions(const Options &options) = 0;
  virtual void CreateWorkerThreads() = 0;
  virtual void Schedule(TimePoint time_point) = 0;
  void WaitThreads() const;

 protected:
  const Options &options_;
  util::WaitGroup wg_;
};
