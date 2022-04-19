#pragma once

#include "options.h"
#include "util/optional.h"
#include "util/string_view.h"

namespace cli {

struct CliArgs {
  util::optional<int> cpu_load;
  util::optional<int> cpu_count;
  util::optional<int> memory_mb;
  util::optional<util::string_view> log_level;
  util::optional<util::string_view> cpu_algorithm;
};

void ParseCommandLineArguments(Options &options, int argc, const char *argv[]);

}  // namespace cli
