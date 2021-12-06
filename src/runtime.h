#pragma once

#include "resmgr.h"

#include <memory>
#include <vector>

class Options;

class Runtime {
 public:
  explicit Runtime(const Options &options);
  void Init();
  void CreateWorkers();
  void MainLoop();
  void JoinWorkers();

 private:
  const Options &options_;
  std::vector<std::unique_ptr<ResourceManager>> managers_;
};
