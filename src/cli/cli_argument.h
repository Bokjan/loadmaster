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
};

}  // namespace cli
