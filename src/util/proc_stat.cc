#include "proc_stat.h"

#include <chrono>

#include "core/platform.h"

#if !IS_WINDOWS
#  include <unistd.h>
#endif

namespace util {

// Throttle the per-process stat refresh rate (all platforms). Reading the
// process CPU counter every scheduling tick (~100ms) is wasteful; 500ms is
// frequent enough for the load controller's rolling average.
constexpr int kProcStatIntervalMS = 500;

ProcStat::ProcStat() {
#if IS_WINDOWS
  process_handle_ = GetCurrentProcess();
#else
  pid_ = getpid();
#endif
}

void ProcStat::UpdateCpuStat(TimePoint now, ForceUpdate force) {
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - time_point_).count();
  if (force != ForceUpdate::kYes && elapsed_ms < kProcStatIntervalMS) {
    return;
  }

  const std::optional<uint64_t> cpu_ns = ReadProcessCpuNs();
  if (!cpu_ns) {
    // Transient read failure: keep the last cached load rather than
    // pretending the process went idle.
    return;
  }

  // Skip the load computation on the very first sample (no prior baseline)
  // and when no wall-clock time has elapsed.
  if (time_point_.time_since_epoch().count() != 0 && elapsed_ms > 0) {
    const uint64_t diff = (*cpu_ns >= prev_cpu_ns_) ? (*cpu_ns - prev_cpu_ns_) : 0;
    const double elapsed_ns = static_cast<double>(elapsed_ms) * 1'000'000.0;
    cpu_load_cached_ = static_cast<int>(static_cast<double>(diff) / elapsed_ns * 100.0);
  }
  time_point_ = now;
  prev_cpu_ns_ = *cpu_ns;
}

}  // namespace util
