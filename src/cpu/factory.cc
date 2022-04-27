#include "factory.h"

#include "util/log.h"
#include "cpu/manager_default.h"
#include "cpu/manager_random_normal.h"

namespace cpu {

std::unique_ptr<core::ResourceManager> CreateResourceManager(const core::Options &options) {
  std::unique_ptr<core::ResourceManager> ret;
  switch (options.GetCpuAlgorithm()) {
    case core::Options::CpuAlgorithm::kDefault:
      ret = std::make_unique<CpuResourceManagerDefault>(options);
      break;
    case core::Options::CpuAlgorithm::kRandomNormal:
      ret = std::make_unique<CpuResourceManagerRandomNormal>(options);
      break;
    default:
      LOG_FATAL("invalid CPU algorithm type [%d]",
                core::Options::EnumToInt(options.GetCpuAlgorithm()));
      break;
  }
  return ret;
}


}  // namespace cpu
