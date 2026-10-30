#include "cli.h"

#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <charconv>
#include <functional>
#include <string>
#include <string_view>

#include "cli_argument.h"
#include "version_string.h"

#include "util/log.h"
#include "util/obfuscate.h"

namespace cli {

namespace {

#if !LOADMASTER_SILENT
// Obfuscated payloads for the human-facing strings printed by `-h`.
// Bundling them here keeps the plaintext out of .rodata: each blob is
// XOR-encoded at compile time and decoded into a stack-local Scoped
// buffer the moment we need to write it out.
//
// We deliberately glue the USAGE header to the OPTIONS body so the
// short "USAGE: ...\n" literal does not survive MSVC's string pool
// pass: very short string literals tend to be retained in .rodata
// even when the only consteval consumer is util::obfuscate::Make.
// The combined long raw string is reliably elided.
constexpr auto kUsage = util::obfuscate::Make(
    R"deli(USAGE: %s [options]
OPTIONS:
    -v                      print version info and quit
    -h                      print this message and quit
    -l  <load>              target CPU usage (100 each core, max 100 x threads), default: min(200, max)
    -L  <log_level>         log level (all/trace/debug/info/warn/error/fatal/off), default: warn
    -c  <thread_count>      worker thread (CPU) count, 0 or default: based on required load
    -ca <algorithm>         CPU schedule algorithm (default/rand_normal), default: default
    -m  <max_memory>        maximum memory (MiB) for wasting, default: 0
    -g  <gpu_load>          per-device GPU compute load (0..100), default: 0 (disabled)
    -gm <gpu_memory>        per-device GPU memory load (MiB), default: 0
    -gi <indices>           GPU device indices, e.g. "0", "0,2,3", or "all"; default: all
    -gv <vendor>            GPU vendor: auto/nvidia/amd/apple/intel/opencl, default: auto
    -ga <algorithm>         GPU schedule algorithm (default/rand_normal), default: default
)deli",
    0xC2374A91u);
#  ifdef _MSC_VER
constexpr auto kBuiltLineMsvc =
    util::obfuscate::Make("Built: " __DATE__ " " __TIME__ ", with MSVC %d.%d.%d\n", 0x5BD49E03u);
#  elif defined(__VERSION__)
constexpr auto kBuiltLineVersioned = util::obfuscate::Make(
    "Built: " __DATE__ " " __TIME__ ", with Compiler " __VERSION__, 0x5BD49E03u);
#  else
constexpr auto kBuiltLinePlain =
    util::obfuscate::Make("Built: " __DATE__ " " __TIME__, 0x5BD49E03u);
#  endif

void PrintUsage(const char *path);
#endif  // !LOADMASTER_SILENT

using FnArgumentProcessor = std::function<bool(int argc, const char *argv[], int &idx)>;

// Result of processing a single argument:
//   * kContinue  : keep parsing
//   * kExitOk    : print-and-exit (e.g. -h / -v)
//   * kFail      : parse error
enum class StepResult { kContinue, kExitOk, kFail };

// `prompt` only feeds LOG_ERROR, which a silent build compiles out.
bool ReadInt(int argc, const char *argv[], int &idx, std::optional<int> &out) {
  [[maybe_unused]] const char *prompt = argv[idx];
  if (idx + 1 >= argc) {
    LOG_ERROR("[%s] failed to read option, arguments insufficient", prompt);
    return false;
  }
  const char *int_str = argv[++idx];
  // std::from_chars rather than sscanf("%d"): the whole token must be a
  // decimal integer (sscanf accepted "12abc" as 12 and "1e3" as 1), and
  // out-of-range input is reported instead of being undefined behaviour.
  const std::string_view sv(int_str);
  int target = 0;
  const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), target);
  if (ec == std::errc::result_out_of_range) {
    LOG_ERROR("[%s] failed to read option, integer out of range: `%s`", prompt, int_str);
    return false;
  }
  if (ec != std::errc{} || ptr != sv.data() + sv.size()) {
    LOG_ERROR("[%s] failed to read option, expect an integer, have: `%s`", prompt, int_str);
    return false;
  }
  out = target;
  return true;
}

bool ReadStringView(int argc, const char *argv[], int &idx, std::optional<std::string_view> &out) {
  [[maybe_unused]] const char *prompt = argv[idx];
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
  // A silent build keeps -h / -v (they still exit cleanly) but prints
  // nothing; their text isn't compiled in.
  const Spec specs[] = {
#if LOADMASTER_SILENT
      {"-h", [](int, const char **, int &) { return true; }, true},
      {"-v", [](int, const char **, int &) { return true; }, true},
#else
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
#endif
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

#if !LOADMASTER_SILENT
void PrintUsage(const char *path) {
  std::puts(VersionString());
  {
    util::obfuscate::ScopedHolder s(kUsage);
    std::printf(s.c_str(), path);
  }
#  ifdef _MSC_VER
  {
    util::obfuscate::ScopedHolder s(kBuiltLineMsvc);
    std::printf(s.c_str(), _MSC_FULL_VER / 10000000, _MSC_FULL_VER / 100000 % 100,
                _MSC_FULL_VER % 100000);
  }
#  elif defined(__VERSION__)
  {
    util::obfuscate::ScopedHolder s(kBuiltLineVersioned);
    std::puts(s.c_str());
  }
#  else
  {
    util::obfuscate::ScopedHolder s(kBuiltLinePlain);
    std::puts(s.c_str());
  }
#  endif
}
#endif  // !LOADMASTER_SILENT

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
