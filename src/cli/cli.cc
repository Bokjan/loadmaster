#include "cli.h"

#include "cpu/cpu.h"
#include "util/log.h"

#include <cstdlib>

#include <functional>
#include <map>

using FnCmdArgHandler = std::function<bool(Options &options, int, const char **, int &)>;

static void PrintUsage(const char *path);

static bool HandlerHelp(Options &options, int argc, const char **argv, int &idx);
static bool HandlerLoad(Options &options, int argc, const char **argv, int &idx);
static bool HandlerConcurrency(Options &options, int argc, const char **argv, int &idx);

void ParseCommandLineArguments(Options &options, int argc, const char *argv[]) {
  static std::map<std::string, FnCmdArgHandler> handlers = {
      {"-h", HandlerHelp}, {"-l", HandlerLoad}, {"-c", HandlerConcurrency}};

  bool terminate = false;

  for (int i = 1; i < argc; ++i) {
    auto f = handlers.find(argv[i]);
    if (f == handlers.end()) {
      LOG_E("unrecognizable option `%s`", argv[i]);
      continue;
    }
    if (!f->second(options, argc, argv, i)) {
      terminate = true;
      break;
    }
  }

  if (terminate) {
    exit(EXIT_SUCCESS);
  }
}

static void PrintUsage(const char *path) {
  printf("USAGE: %s [options] \n", path);
  puts("OPTIONS:");
  puts("    -h                  print this message and quit");
  printf("    -l <load>           target CPU usage (100 each core), default: %d\n",
         kDefaultCpuLoad);
  puts("    -c <thread_count>   worker thread(CPU) count, default: based on required load");
  puts("Built: " __TIMESTAMP__ ", with Compiler " __VERSION__);
}

static bool HandlerHelp(Options &options, int argc, const char **argv, int &idx) {
  PrintUsage(argv[0]);
  return false;
}

static bool HandlerLoad(Options &options, int argc, const char **argv, int &idx) {
  if (idx + 1 >= argc) {
    LOG_E("failed to read -l option, arguments insufficient");
    return false;
  }
  const char *value = argv[++idx];
  int affected = sscanf(value, "%d", &options.load_);
  if (affected != 1) {
    LOG_E("failed to read -l option, expect an integer, have: `%s`", value);
    return false;
  }
  return true;
}

static bool HandlerConcurrency(Options &options, int argc, const char **argv, int &idx) {
  if (idx + 1 >= argc) {
    LOG_E("failed to read -c option, arguments insufficient");
    return false;
  }
  const char *value = argv[++idx];
  int affected = sscanf(value, "%d", &options.count_);
  if (affected != 1) {
    LOG_E("failed to read -c option, expect an integer, have: `%s`", value);
    return false;
  }
  if (options.count_ > cpu::Count()) {
    LOG_E("hardware CPU count: %d, you require %d, abort", cpu::Count(), options.count_);
    return false;
  }
  return true;
}
