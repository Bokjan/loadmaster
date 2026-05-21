#include "cli.h"

#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>

#include "cli_argument.h"
#include "version_string.h"

#include "cpu/stat.h"
#include "util/log.h"

namespace cli {

namespace {

void PrintUsage(const char *path);

using FnArgumentProcessor = std::function<bool(int argc, const char *argv[], int &idx)>;

// Result of processing a single argument:
//   * kContinue  : keep parsing
//   * kExitOk    : print-and-exit (e.g. -h / -v)
//   * kFail      : parse error
enum class StepResult { kContinue, kExitOk, kFail };

bool ReadInt(int argc, const char *argv[], int &idx, std::optional<int> &out) {
  const char *prompt = argv[idx];
  if (idx + 1 >= argc) {
    LOG_ERROR("[%s] failed to read option, arguments insufficient", prompt);
    return false;
  }
  const char *int_str = argv[++idx];
  int target = 0;
#ifdef _MSC_VER
  int affected = sscanf_s(int_str, "%d", &target);
#else
  int affected = std::sscanf(int_str, "%d", &target);
#endif
  if (affected != 1) {
    LOG_ERROR("[%s] failed to read option, expect an integer, have: `%s`", prompt, int_str);
    return false;
  }
  out = target;
  return true;
}

bool ReadStringView(int argc, const char *argv[], int &idx, std::optional<std::string_view> &out) {
  const char *prompt = argv[idx];
  if (idx + 1 >= argc) {
    LOG_ERROR("[%s] failed to read option, arguments insufficient", prompt);
    return false;
  }
  out = argv[++idx];
  return true;
}

StepResult ExtractArguments(CliArgument &cli_args, int argc, const char *argv[]) {
  struct Spec {
    std::string_view flag;
    FnArgumentProcessor handler;
    bool is_terminal;  // -h / -v: print and exit cleanly
  };
  const Spec specs[] = {
      {"-h",
       [&](int, const char **a, int &) {
         PrintUsage(a[0]);
         return true;
       },
       true},
      {"-v",
       [](int, const char **, int &) {
         std::puts(VersionString());
         return true;
       },
       true},
      {"-l", [&](int c, const char **v, int &i) { return ReadInt(c, v, i, cli_args.cpu_load); },
       false},
      {"-c", [&](int c, const char **v, int &i) { return ReadInt(c, v, i, cli_args.cpu_count); },
       false},
      {"-m", [&](int c, const char **v, int &i) { return ReadInt(c, v, i, cli_args.memory_mb); },
       false},
      {"-L",
       [&](int c, const char **v, int &i) { return ReadStringView(c, v, i, cli_args.log_level); },
       false},
      {"-ca",
       [&](int c, const char **v, int &i) {
         return ReadStringView(c, v, i, cli_args.cpu_algorithm);
       },
       false},
      {"-g", [&](int c, const char **v, int &i) { return ReadInt(c, v, i, cli_args.gpu_load); },
       false},
      {"-gm",
       [&](int c, const char **v, int &i) { return ReadInt(c, v, i, cli_args.gpu_memory_mb); },
       false},
      {"-gi",
       [&](int c, const char **v, int &i) { return ReadStringView(c, v, i, cli_args.gpu_indices); },
       false},
      {"-gv",
       [&](int c, const char **v, int &i) { return ReadStringView(c, v, i, cli_args.gpu_vendor); },
       false},
      {"-ga",
       [&](int c, const char **v, int &i) {
         return ReadStringView(c, v, i, cli_args.gpu_algorithm);
       },
       false},
  };

  for (int i = 1; i < argc; ++i) {
    const std::string_view sv(argv[i]);
    auto find = std::find_if(std::begin(specs), std::end(specs),
                             [&sv](const Spec &s) { return sv == s.flag; });
    if (find == std::end(specs)) {
      LOG_ERROR("unrecognizable option `%s`", argv[i]);
      return StepResult::kFail;
    }
    if (!find->handler(argc, argv, i)) {
      return StepResult::kFail;
    }
    if (find->is_terminal) {
      return StepResult::kExitOk;
    }
  }
  return StepResult::kContinue;
}

void PrintUsage(const char *path) {
  std::puts(VersionString());
  std::printf("USAGE: %s [options] \n", path);
  std::puts(R"deli(OPTIONS:
    -v                      print version info and quit
    -h                      print this message and quit
    -l  <load>              target CPU usage (100 each core), default: 200
    -L  <log_level>         log level (trace/debug/info/warn/error/fatal/off), default: warn
    -c  <thread_count>      worker thread (CPU) count, default: based on required load
    -ca <algorithm>         CPU schedule algorithm (default/rand_normal), default: default
    -m  <max_memory>        maximum memory (MiB) for wasting, default: 0
    -g  <gpu_load>          per-device GPU compute load (0..100), default: 0 (disabled)
    -gm <gpu_memory>        per-device GPU memory load (MiB), default: 0
    -gi <indices>           GPU device indices, e.g. "0", "0,2,3", or "all"; default: all
    -gv <vendor>            GPU vendor: auto/nvidia/amd/apple, default: auto
    -ga <algorithm>         GPU schedule algorithm (default/rand_normal), default: default)deli");
#ifdef _MSC_VER
  std::printf("Built: " __DATE__ " " __TIME__ ", with MSVC %d.%d.%d\n", _MSC_FULL_VER / 10000000,
              _MSC_FULL_VER / 100000 % 100, _MSC_FULL_VER % 100000);
#elif defined(__VERSION__)
  std::puts("Built: " __DATE__ " " __TIME__ ", with Compiler " __VERSION__);
#else
  std::puts("Built: " __DATE__ " " __TIME__);
#endif
}

}  // namespace

ParseResult ParseCommandLineArguments(core::Options &options, int argc, const char *argv[]) {
  CliArgument cli_args;
  const StepResult step = ExtractArguments(cli_args, argc, argv);
  if (step == StepResult::kFail) {
    return {EXIT_FAILURE, true};
  }
  if (step == StepResult::kExitOk) {
    return {EXIT_SUCCESS, true};
  }
  if (!options.ProcessCliArguments(cli_args)) {
    return {EXIT_FAILURE, true};
  }
  return {EXIT_SUCCESS, false};
}

}  // namespace cli
