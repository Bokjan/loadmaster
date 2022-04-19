#include "cli.h"

#include "cpu/cpu.h"
#include "util/log.h"
#include "util/make_unique.h"
#include "version.h"

#include <cstdlib>

#include <functional>
#include <map>

namespace cli {

static void PrintUsage(const char *path);

bool ParseInt(int argc, const char *argv[], int &idx, util::optional<int> &value) {
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
  value = target;
  return true;
};

bool ParseStringView(int argc, const char *argv[], int &idx,
                     util::optional<util::string_view> &value) {
  const char *prompt = argv[idx];
  if (idx + 1 >= argc) {
    LOG_FATAL("[%s] failed to read option, arguments insufficient", prompt);
    return false;
  }
  const char *int_str = argv[++idx];
  value = int_str;
  return true;
};

struct CmdArgOption {
  virtual ~CmdArgOption() = default;
  virtual bool Process(int argc, const char *argv[], int &idx) = 0;
};

struct CmdArgOptionHelp : public CmdArgOption {
  bool Process(int argc, const char *argv[], int &idx) {
    PrintUsage(argv[0]);
    return false;
  }
};

struct CmdArgOptionVersion : public CmdArgOption {
  bool Process(int argc, const char *argv[], int &idx) {
    puts(VersionString());
    return false;
  }
};

struct CmdArgOptionInt : public CmdArgOption {
  util::optional<int> &value_ref;
  explicit CmdArgOptionInt(util::optional<int> &ref) : value_ref(ref) {}
  bool Process(int argc, const char *argv[], int &idx) {
    return ParseInt(argc, argv, idx, value_ref);
  }
};

struct CmdArgOptionStringView : public CmdArgOption {
  util::optional<util::string_view> &value_ref;
  explicit CmdArgOptionStringView(util::optional<util::string_view> &ref) : value_ref(ref) {}
  bool Process(int argc, const char *argv[], int &idx) {
    return ParseStringView(argc, argv, idx, value_ref);
  }
};

class CliArgumentProcessor final {
 public:
  CliArgumentProcessor(CliArgs &cli_args) {
    arg_regist_.insert(std::make_pair("-h", util::make_unique<CmdArgOptionHelp>()));
    arg_regist_.insert(std::make_pair("-v", util::make_unique<CmdArgOptionVersion>()));
    arg_regist_.insert(std::make_pair("-l", util::make_unique<CmdArgOptionInt>(cli_args.cpu_load)));
    arg_regist_.insert(
        std::make_pair("-c", util::make_unique<CmdArgOptionInt>(cli_args.cpu_count)));
    arg_regist_.insert(
        std::make_pair("-m", util::make_unique<CmdArgOptionInt>(cli_args.memory_mb)));
    arg_regist_.insert(
        std::make_pair("-L", util::make_unique<CmdArgOptionStringView>(cli_args.log_level)));
    arg_regist_.insert(
        std::make_pair("-ca", util::make_unique<CmdArgOptionStringView>(cli_args.cpu_algorithm)));
  }
  void ExtractArguments(int argc, const char *argv[]) {
    bool terminate = false;
    for (int i = 1; i < argc; ++i) {
      auto f = arg_regist_.find(argv[i]);
      if (f == arg_regist_.end()) {
        LOG_FATAL("unrecognizable option `%s`", argv[i]);
        continue;
      }
      if (!f->second->Process(argc, argv, i)) {
        terminate = true;
        break;
      }
    }
    if (terminate) {
      exit(EXIT_SUCCESS);
    }
  }

 private:
  std::map<std::string, std::unique_ptr<CmdArgOption>> arg_regist_;
};

static void ExtractRawCliData(CliArgs &cli_args, int argc, const char *argv[]) {
  CliArgumentProcessor processor(cli_args);
  processor.ExtractArguments(argc, argv);
}

void ParseCommandLineArguments(Options &options, int argc, const char *argv[]) {
  CliArgs cli_args;
  ExtractRawCliData(cli_args, argc, argv);
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
    -ls <load_sensor>       
    -m  <max_memory>        maximum memory (MiB) for wasting, default: 0 )deli");
  puts("Built: " __DATE__ " " __TIME__ ", with Compiler " __VERSION__);
}

}  // namespace cli
