#include "cli.h"

#include <cstdio>
#include <cstdlib>

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "cli_argument.h"
#include "version_string.h"

#include "cpu/stat.h"
#include "util/log.h"

namespace cli {

static void PrintUsage(const char *path);

using FnArgumentProcessor = std::function<bool(int argc, const char *argv[], int &idx)>;

static bool ProcessorHelp(int argc, const char *argv[], int &idx) {
  PrintUsage(argv[0]);
  return false;
}

static bool ProcessorVersion(int argc, const char *argv[], int &idx) {
  puts(VersionString());
  return false;
}

static bool ProcessorInt(int argc, const char *argv[], int &idx, std::optional<int> &value_ref) {
  const char *prompt = argv[idx];
  if (idx + 1 >= argc) {
    LOG_FATAL("[%s] failed to read option, arguments insufficient", prompt);
    return false;
  }
  const char *int_str = argv[++idx];
  int target = 0;
  int affected = sscanf(int_str, "%d", &target);
  if (affected != 1) {
    LOG_FATAL("[%s] failed to read option, expect an integer, have: `%s`", prompt, int_str);
    return false;
  }
  value_ref = target;
  return true;
}

static bool ProcessorStringView(int argc, const char *argv[], int &idx,
                                std::optional<std::string_view> &value_ref) {
  const char *prompt = argv[idx];
  if (idx + 1 >= argc) {
    LOG_FATAL("[%s] failed to read option, arguments insufficient", prompt);
    return false;
  }
  const char *int_str = argv[++idx];
  value_ref = int_str;
  return true;
}

static void ExtractArguments(CliArgument &cli_args, int argc, const char *argv[]) {
  std::map<std::string_view, FnArgumentProcessor> processors = {
      {"-h", ProcessorHelp},
      {"-v", ProcessorVersion},
      {"-l", std::bind(ProcessorInt, std::placeholders::_1, std::placeholders::_2,
                       std::placeholders::_3, std::ref(cli_args.cpu_load))},
      {"-c", std::bind(ProcessorInt, std::placeholders::_1, std::placeholders::_2,
                       std::placeholders::_3, std::ref(cli_args.cpu_count))},
      {"-m", std::bind(ProcessorInt, std::placeholders::_1, std::placeholders::_2,
                       std::placeholders::_3, std::ref(cli_args.memory_mb))},
      {"-L", std::bind(ProcessorStringView, std::placeholders::_1, std::placeholders::_2,
                       std::placeholders::_3, std::ref(cli_args.log_level))},
      {"-ca", std::bind(ProcessorStringView, std::placeholders::_1, std::placeholders::_2,
                        std::placeholders::_3, std::ref(cli_args.cpu_algorithm))},
  };
  bool terminate = false;
  for (int i = 1; i < argc; ++i) {
    auto f = processors.find(argv[i]);
    if (f == processors.end()) {
      LOG_FATAL("unrecognizable option `%s`", argv[i]);
      continue;
    }
    if (!f->second(argc, argv, i)) {
      terminate = true;
      break;
    }
  }
  if (terminate) {
    exit(EXIT_SUCCESS);
  }
}

void ParseCommandLineArguments(core::Options &options, int argc, const char *argv[]) {
  CliArgument cli_args;
  ExtractArguments(cli_args, argc, argv);
  options.ProcessCliArguments(cli_args);
}

static void PrintUsage(const char *path) {
  puts(VersionString());
  printf("USAGE: %s [options] \n", path);
  puts(R"deli(OPTIONS:
    -v                      print version info and quit
    -h                      print this message and quit
    -l  <load>              target CPU usage (100 each core), default: 200
    -L  <log_level>         log level (trace/debug/info/warn/error/fatal/off), default: warn
    -c  <thread_count>      worker thread (CPU) count, default: based on required load
    -ca <algorithm>         CPU schedule algorithm (default/rand_normal), default: default
    -m  <max_memory>        maximum memory (MiB) for wasting, default: 0 )deli");
  puts("Built: " __DATE__ " " __TIME__ ", with Compiler " __VERSION__);
}

}  // namespace cli
