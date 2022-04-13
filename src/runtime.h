#pragma once

#include "resmgr.h"

#include "util/proc_stat.h"

#include <atomic>
#include <memory>
#include <vector>

class Options;

class Runtime final {
 public:
  explicit Runtime(const Options &options);

  void Init();
  void CreateWorkers();
  void MainLoop();
  void JoinWorkers();

  int GetCpuLoad() const { return proc_stat_.GetCpuLoad(); }
  uint64_t GetMemory() const { return proc_stat_.GetMemory(); }

 private:
  const Options &options_;
  std::vector<std::unique_ptr<ResourceManager>> managers_;
  util::ProcStat proc_stat_;
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
