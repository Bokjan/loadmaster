#pragma once

#include "constants.h"

namespace cli {
struct CliArgs;
}

class Options {
 public:
  enum class CpuAlgo : int { kDefault, kRandomNormal };

  int cpu_load_;
  int cpu_count_;
  CpuAlgo cpu_algorithm_;
  int memory_;

  Options();
  void ProcessCliArguments(const cli::CliArgs &args);
};
