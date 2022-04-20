#pragma once

#include <optional>
#include <string_view>

#include "core/options.h"

namespace cli {

struct CliArgs {
  std::optional<int> cpu_load;
  std::optional<int> cpu_count;
  std::optional<int> memory_mb;
  std::optional<std::string_view> log_level;
  std::optional<std::string_view> cpu_algorithm;
};

void ParseCommandLineArguments(Options &options, int argc, const char *argv[]);

}  // namespace cli
