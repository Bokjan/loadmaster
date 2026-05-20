#include "factory.h"

#include <utility>

#include "manager_default.h"
#include "manager_random_normal.h"

#include "util/log.h"

namespace cpu {

std::unique_ptr<core::ResourceManager> CreateResourceManager(const core::Options &options) {
  switch (options.GetCpuAlgorithm()) {
    case core::Options::CpuAlgorithm::kDefault:
      return std::make_unique<CpuResourceManagerDefault>(options);
    case core::Options::CpuAlgorithm::kRandomNormal:
      return std::make_unique<CpuResourceManagerRandomNormal>(options);
  }
  // Unreachable: Options validates the algorithm at parse time. If we ever
  // see this, it's a bug.
  LOG_FATAL("invalid CPU algorithm type [%d]", core::Options::EnumToInt(options.GetCpuAlgorithm()));
}

}  // namespace cpu
