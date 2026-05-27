#pragma once

#include <optional>
#include <string_view>

namespace cli {

struct CliArgument {
  std::optional<int> cpu_load;
  std::optional<int> cpu_count;
  std::optional<int> memory_mb;
  std::optional<std::string_view> log_level;
  std::optional<std::string_view> cpu_algorithm;
  // GPU
  std::optional<int> gpu_load;
  std::optional<int> gpu_memory_mb;
  std::optional<std::string_view> gpu_indices;    // e.g. "0,2,3" or "all"
  std::optional<std::string_view> gpu_vendor;     // auto / nvidia / amd / apple / intel / opencl
  std::optional<std::string_view> gpu_algorithm;  // default / rand_normal
};

}  // namespace cli
