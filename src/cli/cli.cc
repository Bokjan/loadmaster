#include "cli.h"

#include "cpu/cpu.h"
#include "util/log.h"

#include <cstdlib>

#include <functional>
#include <map>
namespace cli {

using FnCmdArgHandler = std::function<bool(Options &options, int, const char **, int &)>;

static void PrintUsage(const char *path);

static bool ReadInt(int &target, int argc, const char **argv, int &idx, const char *prompt = "");
static bool ReadString(std::string &target, int argc, const char **argv, int &idx,
                       const char *prompt = "");

static bool HandlerHelp(Options &options, int argc, const char **argv, int &idx);
static bool HandlerVersion(Options &options, int argc, const char **argv, int &idx);
static bool HandlerCpuLoad(Options &options, int argc, const char **argv, int &idx);
static bool HandlerCpuCount(Options &options, int argc, const char **argv, int &idx);
static bool HandlerMemory(Options &options, int argc, const char **argv, int &idx);
static bool HandlerLogLevel(Options &options, int argc, const char **argv, int &idx);
static bool HandlerCpuAlgorithm(Options &options, int argc, const char **argv, int &idx);

void ParseCommandLineArguments(Options &options, int argc, const char *argv[]) {
  static std::map<std::string, FnCmdArgHandler> handlers = {
      {"-v", HandlerVersion},      {"-h", HandlerHelp},   {"-l", HandlerCpuLoad},
      {"-c", HandlerCpuCount},     {"-m", HandlerMemory}, {"-L", HandlerLogLevel},
      {"-ca", HandlerCpuAlgorithm}};

  bool terminate = false;

  for (int i = 1; i < argc; ++i) {
    auto f = handlers.find(argv[i]);
    if (f == handlers.end()) {
      LOG_FATAL("unrecognizable option `%s`", argv[i]);
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
  puts("    -v                      print version info and quit");
  puts("    -h                      print this message and quit");
  printf("    -l  <load>              target CPU usage (100 each core), default: %d\n",
         kDefaultCpuLoad);
  puts(
      "    -L  <log_level>         log level (trace/debug/info/warn/error/fatal/off), default: "
      "warn");
  puts("    -c  <thread_count>      worker thread (CPU) count, default: based on required load");
  puts(
      "    -ca <algorithm>         CPU schedule algorithm (default/rand_normal), default: default");
  puts("    -m  <max_memory>        maximum memory (MiB) for wasting, default: 0");
  puts("Built: " __TIMESTAMP__ ", with Compiler " __VERSION__);
}

static bool HandlerHelp(Options &options, int argc, const char **argv, int &idx) {
  PrintUsage(argv[0]);
  return false;
}

static bool HandlerVersion(Options &options, int argc, const char **argv, int &idx) {
  printf("%s %d.%d.%d", kVersionProject, kVersionMajor, kVersionMinor, kVersionPatch);
  if (kVersionSuffix[0] != '\0') {
    printf("-%s", kVersionSuffix);
  }
  puts("");
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
    LOG_FATAL("hardware CPU count: %d, you require %d, abort", cpu::Count(), options.cpu_count_);
    return false;
  }
  return true;
}

static bool HandlerMemory(Options &options, int argc, const char **argv, int &idx) {
  int memory_mib = 0;
  if (!ReadInt(memory_mib, argc, argv, idx, "-m")) {
    return false;
  }
  options.memory_ = memory_mib * kMebiByte;
  return true;
}

static bool HandlerLogLevel(Options &options, int argc, const char **argv, int &idx) {
  std::string level_str;
  if (!ReadString(level_str, argc, argv, idx, "-L")) {
    return false;
  }
  if (!util::logger_internal::g_logger->SetLevel(level_str.c_str())) {
    LOG_FATAL("invalid log level [%s]", level_str.c_str());
    return false;
  }
  return true;
}

static bool HandlerCpuAlgorithm(Options &options, int argc, const char **argv, int &idx) {
  std::string algo_str;
  if (!ReadString(algo_str, argc, argv, idx, "-ca")) {
    return false;
  }
  static std::map<std::string, Options::ScheduleAlgorithm> map = {
      {"default", Options::ScheduleAlgorithm::kDefault},
      {"rand_normal", Options::ScheduleAlgorithm::kRandomNormal}};
  auto find = map.find(algo_str);
  if (find == map.end()) {
    LOG_FATAL("invalid CPU algorithm [%s]", algo_str.c_str());
    return false;
  }
  options.cpu_algorithm_ = find->second;
  return true;
}

static bool ReadInt(int &target, int argc, const char **argv, int &idx, const char *prompt) {
  if (idx + 1 >= argc) {
    LOG_FATAL("[%s] failed to read option, arguments insufficient", prompt);
    return false;
  }
  const char *value = argv[++idx];
  int affected = sscanf(value, "%d", &target);
  if (affected != 1) {
    LOG_FATAL("[%s] failed to read option, expect an integer, have: `%s`", prompt, value);
    return false;
  }
  return true;
}

static bool ReadString(std::string &target, int argc, const char **argv, int &idx,
                       const char *prompt) {
  if (idx + 1 >= argc) {
    LOG_FATAL("[%s] failed to read option, arguments insufficient", prompt);
    return false;
  }
  const char *value = argv[++idx];
  target.assign(value);
  return true;
}

}  // namespace cli
