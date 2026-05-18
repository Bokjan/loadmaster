#include "options.h"

#include <algorithm>
#include <string>

#include "constants.h"

#include "cli/cli_argument.h"
#include "cpu/stat.h"
#include "util/log.h"

namespace core {

Options::Options()
    : cpu_load_(kDefaultCpuLoad),
      cpu_count_(0),
      cpu_algorithm_(CpuAlgorithm::kDefault),
      memory_bytes_(static_cast<int64_t>(kDefaultMemoryLoadMiB) * kMebiByte) {}

bool Options::ProcessCliArguments(const cli::CliArgument &args) {
  // CPU load
  if (args.cpu_load) {
    cpu_load_ = args.cpu_load.value();
  }
  // CPU count
  if (args.cpu_count) {
    if (args.cpu_count.value() > cpu::CoreCount()) {
      LOG_ERROR("hardware CPU count: %u, you require %d, abort", cpu::CoreCount(),
                args.cpu_count.value());
      return false;
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
      LOG_ERROR("invalid CPU algorithm [%s]", args.cpu_algorithm.value().data());
      return false;
    }
    cpu_algorithm_ = find->second;
  }
  // Memory (input is MiB)
  if (args.memory_mb) {
    const int mib = args.memory_mb.value();
    if (mib < 0) {
      LOG_ERROR("invalid memory MiB: %d", mib);
      return false;
    }
    memory_bytes_ = static_cast<int64_t>(mib) * kMebiByte;
  }
  // Log level
  if (args.log_level) {
    if (!util::logger_internal::g_default_logger->SetLevel(args.log_level.value().data())) {
      LOG_ERROR("invalid log level [%s]", args.log_level.value().data());
      return false;
    }
  }
  return true;
}

}  // namespace core
