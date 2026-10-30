#include "source.h"

#include "core/platform.h"

#if IS_WINDOWS
#  include "nvml_source.h"
#  include "pdh_source.h"
#elif IS_MACOS
#  include <unistd.h>
#  include "ioreg_source.h"
#elif IS_LINUX
#  include <unistd.h>
#  include "drm_source.h"
#  include "kfd_source.h"
#  include "nvml_source.h"
#endif

namespace gpu::contention {

std::vector<std::unique_ptr<Source>> CreatePlatformSources() {
  std::vector<std::unique_ptr<Source>> out;
#if IS_WINDOWS
  const auto self = ::GetCurrentProcessId();
  auto nvml = std::make_unique<NvmlSource>(static_cast<int>(self));
  if (nvml->Available()) {
    out.push_back(std::move(nvml));
  }
  auto pdh = std::make_unique<PdhSource>(self);
  if (pdh->Available()) {
    out.push_back(std::move(pdh));
  }
#elif IS_MACOS
  out.push_back(std::make_unique<IoRegSource>(static_cast<int>(::getpid())));
#elif IS_LINUX
  const int self = static_cast<int>(::getpid());
  auto nvml = std::make_unique<NvmlSource>(self);
  if (nvml->Available()) {
    out.push_back(std::move(nvml));
  }
  if (KfdSource::Available("")) {
    out.push_back(std::make_unique<KfdSource>("", self));
  }
  out.push_back(std::make_unique<DrmSource>("", self));
#endif
  // BSD: no vendor library or kernel interface lists GPU users; the
  // monitor warns once per device and never yields.
  return out;
}

}  // namespace gpu::contention
