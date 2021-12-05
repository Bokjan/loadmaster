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

  explicit WorkerContext(int id, util::WaitGroup &wg) : id_(id), parent_wg_(wg) {}
  virtual void Loop() = 0;

 private:
  util::WaitGroup &parent_wg_;
  friend class WorkerLoopGuard;
};

class WaitGroupDoneGuard final {
 public:
  explicit WaitGroupDoneGuard(util::WaitGroup &wg);
  ~WaitGroupDoneGuard();

 private:
  util::WaitGroup &wg_;
};

class WorkerLoopGuard final {
 public:
  explicit WorkerLoopGuard(WorkerContext &ctx);
  ~WorkerLoopGuard() = default;

 private:
  WaitGroupDoneGuard wgd_guard_;
};

class ResourceManager {
 public:
  explicit ResourceManager(const Options &options) : options_(options) {}
  virtual bool Init() = 0;
  virtual void CreateWorkerThreads() = 0;
  virtual void Schedule(TimePoint time_point) = 0;
  void WaitThreads() const;

 protected:
  const Options &options_;
  util::WaitGroup wg_;
};
