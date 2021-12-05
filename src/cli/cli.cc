#include "cli.h"

#include "cpu/cpu.h"
#include "util/log.h"

#include <cstdlib>

#include <functional>
#include <map>

using FnCmdArgHandler = std::function<bool(Options &options, int, const char **, int &)>;

static void PrintUsage(const char *path);

static bool ReadInt(int &target, int argc, const char **argv, int &idx, const char *prompt = "");

static bool HandlerHelp(Options &options, int argc, const char **argv, int &idx);
static bool HandlerCpuLoad(Options &options, int argc, const char **argv, int &idx);
static bool HandlerCpuCount(Options &options, int argc, const char **argv, int &idx);
static bool HandlerMemory(Options &options, int argc, const char **argv, int &idx);

void ParseCommandLineArguments(Options &options, int argc, const char *argv[]) {
  static std::map<std::string, FnCmdArgHandler> handlers = {
      {"-h", HandlerHelp}, {"-l", HandlerCpuLoad}, {"-c", HandlerCpuCount}, {"-m", HandlerMemory}};

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
  puts("    -m <max_memory>     maximum memory (MiB) for wasting, default: 0");
  puts("Built: " __TIMESTAMP__ ", with Compiler " __VERSION__);
}

static bool HandlerHelp(Options &options, int argc, const char **argv, int &idx) {
  PrintUsage(argv[0]);
  return false;
}

static bool HandlerCpuLoad(Options &options, int argc, const char **argv, int &idx) {
  return ReadInt(options.cpu_load_, argc, argv, idx, "-l");
}

static bool HandlerCpuCount(Options &options, int argc, const char **argv, int &idx) {
  if (!ReadInt(options.cpu_count_, argc, argv, idx, "-c")) {
    return false;
  }
  if (options.cpu_count_ > cpu::Count()) {
    LOG_E("hardware CPU count: %d, you require %d, abort", cpu::Count(), options.cpu_count_);
    return false;
  }
  return true;
}

static bool HandlerMemory(Options &options, int argc, const char **argv, int &idx) {
  int memory_mib = 0;
  if (!ReadInt(memory_mib, argc, argv, idx, "-m")) {
    return false;
  }
  options.memory_ = memory_mib * 1024 * 1024;
  return true;
}

static bool ReadInt(int &target, int argc, const char **argv, int &idx, const char *prompt) {
  if (idx + 1 >= argc) {
    LOG_E("[%s] failed to read option, arguments insufficient", prompt);
    return false;
  }
  const char *value = argv[++idx];
  int affected = sscanf(value, "%d", &target);
  if (affected != 1) {
    LOG_E("[%s] failed to read option, expect an integer, have: `%s`", prompt, value);
    return false;
  }
  return true;
}
