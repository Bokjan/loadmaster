#pragma once

#include "options.h"
#include "util/optional.h"
#include "util/string_view.h"

namespace cli {

struct CliArgs {
  util::optinal<int> cpu_load;
  util::optinal<int> cpu_count;
  util::optinal<int> memory_mb;
  util::optinal<util::string_view> log_level;
  util::optinal<util::string_view> cpu_algorithm;
};

void ParseCommandLineArguments(Options &options, int argc, const char *argv[]);

}  // namespace cli
