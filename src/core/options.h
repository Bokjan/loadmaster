#pragma once

#include <cstdint>

namespace cli {
struct CliArgument;
}

namespace core {

class Options final {
 public:
  enum class CpuAlgorithm : int { kDefault, kRandomNormal };

  Options();

  // Apply parsed CLI arguments. Returns false on validation failure.
  // On failure, the caller is expected to exit with EXIT_FAILURE.
  bool ProcessCliArguments(const cli::CliArgument &args);

  template <typename T>
  static inline int EnumToInt(T value) {
    return static_cast<int>(value);
  }

  int GetCpuLoad() const { return cpu_load_; }
  int GetCpuCount() const { return cpu_count_; }
  CpuAlgorithm GetCpuAlgorithm() const { return cpu_algorithm_; }
  int64_t GetMemoryBytes() const { return memory_bytes_; }

 private:
  int cpu_load_;                // 100 per core
  int cpu_count_;               // worker thread count
  CpuAlgorithm cpu_algorithm_;  // as shown
  int64_t memory_bytes_;        // memory load in bytes
};

}  // namespace core
