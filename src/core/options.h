#pragma once

#include "cli/cli_argument.h"

namespace core {

class Options final {
 public:
  enum class CpuAlgorithm : int { kDefault, kRandomNormal };

  Options();

  void ProcessCliArguments(const cli::CliArgument &args);
  template <typename T>
  static inline int EnumToInt(T value) {
    return static_cast<int>(value);
  }

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

}  // namespace core
