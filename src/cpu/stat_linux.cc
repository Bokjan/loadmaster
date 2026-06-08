#include "core/platform.h"

#if IS_LINUX

#  include "stat.h"
#  include "stat_internal.h"

#  include <cinttypes>
#  include <cstdio>
#  include <memory>
#  include <string_view>

#  include "core/constants.h"
#  include "util/clock.h"
#  include "util/log.h"

#  define LMPU64 "%" PRIu64

namespace cpu {

namespace {

// RAII wrapper for FILE*.
struct FileCloser {
  void operator()(FILE *fp) const noexcept {
    if (fp != nullptr) {
      std::fclose(fp);
    }
  }
};
using UniqueFile = std::unique_ptr<FILE, FileCloser>;

}  // namespace

namespace internal {

uint64_t BusyJiffies(const ProcStatFields &f) {
  // "Not idle" -> busy. idle and iowait are excluded; guest/guest_nice are
  // already folded into user/nice by the kernel and must not be re-added.
  return f.user + f.nice + f.system + f.irq + f.softirq + f.steal;
}

uint64_t JiffiesToNs(uint64_t jiffies) {
  // jiffy -> ns: jiffies * (ms per jiffy) * 1e6 ns/ms.
  return jiffies * static_cast<uint64_t>(util::GetJiffyMillisecond()) * 1'000'000ULL;
}

}  // namespace internal

std::optional<uint64_t> ReadSystemBusyNs() {
  // Constrain `%s` width to avoid buffer overflow (kSmallBufferLength == 128).
  char buffer[kSmallBufferLength];
  UniqueFile fp(std::fopen("/proc/stat", "r"));
  if (!fp) {
    LOG_ERROR("failed to open /proc/stat");
    return std::nullopt;
  }
  internal::ProcStatFields f{};
  constexpr auto kStatFormat =
      "%127s" LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64 LMPU64;
  const int count = std::fscanf(fp.get(), kStatFormat, buffer, &f.user, &f.nice, &f.system, &f.idle,
                                &f.iowait, &f.irq, &f.softirq, &f.steal, &f.guest, &f.guest_nice);
  if (count != 11) {
    LOG_ERROR("failed to `fscanf` from /proc/stat, get val: %d, expect: 11", count);
    return std::nullopt;
  }
  if (!std::string_view(buffer).starts_with("cpu")) {
    LOG_ERROR("failed to read /proc/stat, have: %s, expect: cpu", buffer);
    return std::nullopt;
  }
  return internal::JiffiesToNs(internal::BusyJiffies(f));
}

}  // namespace cpu

#endif  // IS_LINUX
