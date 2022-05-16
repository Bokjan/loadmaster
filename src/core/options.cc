#include "options.h"

#include <cstdlib>

#include <algorithm>
#include <string>
#include <thread>

#include "constants.h"

#include "cpu/stat.h"
#include "util/log.h"

namespace core {

Options::Options()
    : cpu_load_(kDefaultCpuLoad),
      cpu_count_(0),
      cpu_algorithm_(CpuAlgorithm::kDefault),
      memory_(kDefaultMemoryLoadMiB) {}

void Options::ProcessCliArguments(const cli::CliArgument &args) {
  bool success = false;
  do {
    // CPU load
    if (args.cpu_load) {
      cpu_load_ = args.cpu_load.value();
    }
    // CPU count
    if (args.cpu_count) {
      if (args.cpu_count.value() > cpu::CoreCount()) {
        LOG_FATAL("hardware CPU count: %u, you require %d, abort", cpu::CoreCount(),
                  args.cpu_count.value());
        break;
      }
      cpu_count_ = args.cpu_count.value();
    }
    // CPU scheduling algorithm
    if (args.cpu_algorithm) {
      using SvAlgoPair = std::pair<std::string_view, Options::CpuAlgorithm>;
      static const SvAlgoPair algorithm_pairs[] = {
          {"default", Options::CpuAlgorithm::kDefault},
          {"rand_normal", Options::CpuAlgorithm::kRandomNormal}};
      auto find = std::find_if(
          std::begin(algorithm_pairs), std::end(algorithm_pairs),
          [&args](const SvAlgoPair &pair) { return pair.first == args.cpu_algorithm; });
      if (find == std::end(algorithm_pairs)) {
        LOG_FATAL("invalid CPU algorithm [%s]", args.cpu_algorithm.value().data());
        break;
      }
      auto [_, cpu_algorithm] = *find;
      cpu_algorithm_ = cpu_algorithm;
    }
    // Memory
    if (args.memory_mb) {
      memory_ = args.memory_mb.value() * kMebiByte;
    }
    // Log level
    if (args.log_level) {
      if (!util::logger_internal::g_default_logger->SetLevel(args.log_level.value().data())) {
        LOG_FATAL("invalid log level [%s]", args.log_level.value().data());
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

}  // namespace core
