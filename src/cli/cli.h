#pragma once

#include "core/options.h"

namespace cli {

// Parses CLI arguments into `options`. When `should_exit` is set the caller
// must exit immediately with `exit_code`:
//   * EXIT_SUCCESS, should_exit = true  -> -h/-v printed, exit cleanly
//   * EXIT_SUCCESS, should_exit = false -> normal run
//   * EXIT_FAILURE, should_exit = true  -> parse/validation error
struct ParseResult {
  int exit_code;
  bool should_exit;
};

ParseResult ParseCommandLineArguments(core::Options &options, int argc, const char *argv[]);

}  // namespace cli
