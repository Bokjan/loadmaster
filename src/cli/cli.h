#pragma once

#include "options.h"
#include "util/optional.h"
#include "util/string_view.h"

namespace cli {

struct CliArgs {
  util::Optional<int> cpu_load;
  util::Optional<int> cpu_count;
  util::Optional<int> memory_mb;
  util::Optional<util::StringView> log_level;
  util::Optional<util::StringView> cpu_algorithm;
};

void ParseCommandLineArguments(Options &options, int argc, const char *argv[]);

}
