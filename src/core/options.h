#pragma once

#include "constants.h"

namespace cli {
struct CliArgs;
}

template <typename T>
inline int EnumToInt(T value) {
  return static_cast<int>(value);
}

class Options final {
 public:
  enum class CpuAlgorithm : int { kDefault, kRandomNormal };

  Options();

  void ProcessCliArguments(const cli::CliArgs &args);

  int GetCpuLoad() const { return cpu_load_; }
  int GetCpuCount() const { return cpu_count_; }
  CpuAlgorithm GetCpuAlgorithm() const { return cpu_algorithm_; }
  int GetMemoryMiB() const { return memory_; }

 private:
  int cpu_load_;                // 100 per core
  int cpu_count_;               // worker thread count
  CpuAlgorithm cpu_algorithm_;  // as shown
  int memory_;                  // in MiB
};
