#pragma once

#include <cstdint>
#include <vector>

namespace cli {
struct CliArgument;
}

namespace core {

class Options final {
 public:
  enum class CpuAlgorithm : int { kDefault, kRandomNormal };
  enum class GpuAlgorithm : int { kDefault, kRandomNormal };
  // GPU vendor selection. `kApple` drives the Apple Silicon / Intel Mac
  // GPU through Metal and is only meaningful on macOS builds; on other
  // platforms it is accepted by the parser but the corresponding factory
  // path is compiled out and immediately yields "no device".
  enum class GpuVendor : int { kAuto, kNvidia, kAmd, kApple };

  Options();

  // Apply parsed CLI arguments. Returns false on validation failure.
  // On failure, the caller is expected to exit with EXIT_FAILURE.
  bool ProcessCliArguments(const cli::CliArgument &args);

  // CPU
  int GetCpuLoad() const { return cpu_load_; }
  int GetCpuCount() const { return cpu_count_; }
  CpuAlgorithm GetCpuAlgorithm() const { return cpu_algorithm_; }

  // Memory
  int64_t GetMemoryBytes() const { return memory_bytes_; }

  // GPU
  int GetGpuLoad() const { return gpu_load_; }
  int64_t GetGpuMemoryBytes() const { return gpu_memory_bytes_; }
  // Empty vector means "use every device the chosen vendor exposes".
  const std::vector<int> &GetGpuIndices() const { return gpu_indices_; }
  bool GpuUseAllDevices() const { return gpu_indices_.empty(); }
  GpuVendor GetGpuVendor() const { return gpu_vendor_; }
  GpuAlgorithm GetGpuAlgorithm() const { return gpu_algorithm_; }

 private:
  // CPU
  int cpu_load_;
  int cpu_count_;
  CpuAlgorithm cpu_algorithm_;
  // Memory
  int64_t memory_bytes_;
  // GPU
  int gpu_load_;
  int64_t gpu_memory_bytes_;
  std::vector<int> gpu_indices_;
  GpuVendor gpu_vendor_;
  GpuAlgorithm gpu_algorithm_;
};

}  // namespace core
