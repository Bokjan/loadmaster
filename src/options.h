#pragma once

#include "constants.h"

namespace cli {
struct CliArgs;
}

class Options final {
 public:
  enum class CpuAlgo : int { kDefault, kRandomNormal };

  Options();

  void ProcessCliArguments(const cli::CliArgs &args);

  int CpuLoad() const { return cpu_load_; }
  int CpuCount() const { return cpu_count_; }
  CpuAlgo CpuAlgorithm() const { return cpu_algorithm_; }
  int Memory() const { return memory_; }

 private:
  int cpu_load_;           // 100 per core
  int cpu_count_;          // worker thread count
  CpuAlgo cpu_algorithm_;  // as shown
  int memory_;             // in MiB
};
