#include "options.h"

#include <cstdlib>

#include <map>

#include "cli/cli.h"
#include "cpu/cpu.h"
#include "util/log.h"

Options::Options()
    : cpu_load_(kDefaultCpuLoad),
      cpu_count_(0),
      cpu_algorithm_(CpuAlgo::kDefault),
      memory_(kDefaultMemoryLoadMiB) {}

void Options::ProcessCliArguments(const cli::CliArgs &args) {
  bool success = false;
  do {
    // CPU load
    if (args.cpu_load) {
      cpu_load_ = args.cpu_load.value();
    }
    // CPU count
    if (args.cpu_count) {
      if (args.cpu_count.value() > cpu::Count()) {
        LOG_FATAL("hardware CPU count: %d, you require %d, abort", cpu::Count(),
                  args.cpu_count.value());
        break;
      }
      cpu_count_ = args.cpu_count.value();
    }
    // CPU scheduling algorithm
    if (args.cpu_algorithm) {
      static std::map<std::string, Options::CpuAlgo> map = {
          {"default", Options::CpuAlgo::kDefault},
          {"rand_normal", Options::CpuAlgo::kRandomNormal}};
      auto find = map.find(args.cpu_algorithm.value().c_str());
      if (find == map.end()) {
        LOG_FATAL("invalid CPU algorithm [%s]", args.cpu_algorithm.value().c_str());
        break;
      }
      cpu_algorithm_ = find->second;
    }
    // Memory
    if (args.memory_mb) {
      memory_ = args.memory_mb.value() * kMebiByte;
    }
    // Log level
    if (args.log_level) {
      if (!util::logger_internal::g_logger->SetLevel(args.log_level.value().c_str())) {
        LOG_FATAL("invalid log level [%s]", args.log_level.value().c_str());
        break;
      }
    }
    // All success
    success = true;
  } while (false);
  if (!success) {
    exit(EXIT_SUCCESS);
  }
}
