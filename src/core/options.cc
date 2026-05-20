#include "options.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <string_view>

#include "constants.h"

#include "cli/cli_argument.h"
#include "cpu/stat.h"
#include "util/log.h"

namespace core {

namespace {

// Parse comma-separated integer indices, e.g. "0,2,3". Special token "all"
// returns an empty vector (meaning "use everything available").
bool ParseGpuIndices(std::string_view spec, std::vector<int> &out) {
  out.clear();
  if (spec == "all") {
    return true;
  }
  size_t start = 0;
  while (start <= spec.size()) {
    const size_t comma = spec.find(',', start);
    const size_t end = (comma == std::string_view::npos) ? spec.size() : comma;
    std::string_view token = spec.substr(start, end - start);
    if (token.empty()) {
      return false;
    }
    int idx = -1;
    auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), idx);
    if (ec != std::errc{} || ptr != token.data() + token.size() || idx < 0) {
      return false;
    }
    out.push_back(idx);
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  // Reject duplicates.
  std::vector<int> sorted = out;
  std::sort(sorted.begin(), sorted.end());
  if (std::unique(sorted.begin(), sorted.end()) != sorted.end()) {
    return false;
  }
  return !out.empty();
}

}  // namespace

Options::Options()
    : cpu_load_(kDefaultCpuLoad),
      cpu_count_(0),
      cpu_algorithm_(CpuAlgorithm::kDefault),
      memory_bytes_(static_cast<int64_t>(kDefaultMemoryLoadMiB) * kMebiByte),
      gpu_load_(kDefaultGpuLoad),
      gpu_memory_bytes_(static_cast<int64_t>(kDefaultGpuMemoryMiB) * kMebiByte),
      gpu_indices_(),
      gpu_vendor_(GpuVendor::kAuto) {}

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
    auto find =
        std::find_if(std::begin(algorithm_pairs), std::end(algorithm_pairs),
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
  // GPU load
  if (args.gpu_load) {
    const int load = args.gpu_load.value();
    if (load < 0 || load > kGpuMaxLoadPerDevice) {
      LOG_ERROR("invalid GPU load (per device, 0..%d): %d", kGpuMaxLoadPerDevice, load);
      return false;
    }
    gpu_load_ = load;
  }
  // GPU memory
  if (args.gpu_memory_mb) {
    const int mib = args.gpu_memory_mb.value();
    if (mib < 0) {
      LOG_ERROR("invalid GPU memory MiB: %d", mib);
      return false;
    }
    gpu_memory_bytes_ = static_cast<int64_t>(mib) * kMebiByte;
  }
  // GPU indices
  if (args.gpu_indices) {
    if (!ParseGpuIndices(args.gpu_indices.value(), gpu_indices_)) {
      LOG_ERROR("invalid GPU indices spec: [%s]", args.gpu_indices.value().data());
      return false;
    }
  }
  // GPU vendor
  if (args.gpu_vendor) {
    using SvVendorPair = std::pair<std::string_view, Options::GpuVendor>;
    static const SvVendorPair vendor_pairs[] = {{"auto", GpuVendor::kAuto},
                                                {"nvidia", GpuVendor::kNvidia},
                                                {"amd", GpuVendor::kAmd},
                                                {"apple", GpuVendor::kApple}};
    auto find =
        std::find_if(std::begin(vendor_pairs), std::end(vendor_pairs),
                     [&args](const SvVendorPair &pair) { return pair.first == args.gpu_vendor; });
    if (find == std::end(vendor_pairs)) {
      LOG_ERROR("invalid GPU vendor [%s] (expected: auto/nvidia/amd/apple)",
                args.gpu_vendor.value().data());
      return false;
    }
    gpu_vendor_ = find->second;
  }
  return true;
}

}  // namespace core
