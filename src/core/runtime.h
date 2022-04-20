#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include "core/resource_manager.h"
#include "util/proc_stat.h"

class Options;

class Runtime final {
 public:
  explicit Runtime(const Options &options);

  void Init();
  void CreateWorkers();
  void MainLoop();
  void JoinWorkers();

 private:
  const Options &options_;
  std::vector<std::unique_ptr<ResourceManager>> managers_;

  TimePoint NextSchedulingTime(TimePoint current_start);
};

class RunningFlag final {
 public:
  static RunningFlag &Get() {
    static RunningFlag instance;
    return instance;
  }
  bool IsRunning() const { return !stop_flag_.load(); }
  void Stop() { stop_flag_.store(true); }

 private:
  std::atomic_bool stop_flag_;
  RunningFlag() { stop_flag_.store(false); }
};
