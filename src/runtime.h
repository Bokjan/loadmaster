#pragma once

#include "resmgr.h"

#include "util/proc_stat.h"

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

 private:
  const Options &options_;
  std::vector<std::unique_ptr<ResourceManager>> managers_;
  util::ProcStat proc_stat_;
};
