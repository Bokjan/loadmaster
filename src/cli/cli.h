#pragma once

#include "core/options.h"

namespace cli {

// Parses CLI arguments into `options`. Returns the desired process exit
// code: EXIT_SUCCESS to continue running, otherwise the caller should exit
// with that code immediately.
//
// Special return values:
//   * EXIT_SUCCESS, ran_help = true  -> -h/-v printed, exit cleanly
//   * EXIT_SUCCESS, ran_help = false -> normal run
//   * EXIT_FAILURE                   -> parse/validation error
struct ParseResult {
  int exit_code;
  bool should_exit;
};

ParseResult ParseCommandLineArguments(core::Options &options, int argc, const char *argv[]);

}  // namespace cli
