#include "manager_random_normal.h"

#include <algorithm>
#include <utility>

#include "constants.h"

#include "core/options.h"
#include "util/log.h"

namespace gpu {

GpuResourceManagerRandomNormal::GpuResourceManagerRandomNormal(
    const core::Options &options, std::vector<std::pair<int, std::unique_ptr<GpuDevice>>> devices)
    : GpuResourceManager(options, std::move(devices)),
      generator_(std::random_device{}()),
      dist_(kGpuRandNormalMu, kGpuRandNormalSigma) {
  GenerateBaseSchedulePoints();
}

bool GpuResourceManagerRandomNormal::Init() {
  // Bring up devices and workers via the base implementation. After this
  // call returns true, `workers_` contains one entry per usable device
  // and `pending_devices_` has been cleared.
  if (!GpuResourceManager::Init()) {
    return false;
  }
  // Per-device shuffle. Independent shuffles ensure the boards' load
  // walks aren't correlated. Each device starts at point_idx=0 with a
  // last_change of "long ago", so the very first Schedule() tick will
  // immediately drive the first load value.
  states_.resize(workers_.size());
  for (auto &s : states_) {
    s.schedule_points = ShuffledPoints();
  }
  return true;
}

void GpuResourceManagerRandomNormal::Schedule(TimePoint time_point) {
  for (size_t i = 0; i < workers_.size(); ++i) {
    AdvanceDevice(i, time_point);
  }
}

void GpuResourceManagerRandomNormal::AdvanceDevice(size_t worker_index, TimePoint time_point) {
  auto &state = states_[worker_index];
  // First tick: state.last_change is epoch-zero -> elapsed_ms is huge ->
  // we fire immediately, which is what we want.
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(time_point - state.last_change).count();
  if (elapsed_ms < kGpuRandNormalScheduleIntervalMS) {
    return;
  }
  // Wrapped around: re-shuffle so the next 5-min window picks a new
  // ordering of the same values.
  if (state.point_idx == 0 && state.last_change != TimePoint{}) {
    state.schedule_points = ShuffledPoints();
  }
  const int load_percent = state.schedule_points[state.point_idx];
  workers_[worker_index]->SetLoadSet(LoadPercentToBusyMs(load_percent));
  LOG_DEBUG("gpu rand_normal: worker=%d point_idx=%d load=%d", workers_[worker_index]->id(),
            state.point_idx, load_percent);

  ++state.point_idx;
  if (state.point_idx == static_cast<int>(state.schedule_points.size())) {
    state.point_idx = 0;
  }
  state.last_change = time_point;
}

void GpuResourceManagerRandomNormal::GenerateBaseSchedulePoints() {
  base_schedule_points_.clear();
  // Same construction as the CPU rand_normal scheduler: discretise the
  // CDF over [-CDF_target, CDF_target] standard deviations and let each
  // bin contribute proportionally to its area. The `factor` is chosen so
  // the values average out to options_.GetGpuLoad() over one full
  // period.
  const double x_pos_upper = dist_.FindXAxisPositionCDF(kGpuRandNormalCdfTarget);
  const double x_pos_lower = dist_.GetMean() - (x_pos_upper - dist_.GetMean());
  const double step = (x_pos_upper - x_pos_lower) / kGpuRandNormalSchedulePointCount;
  const double factor =
      options_.GetGpuLoad() * kGpuRandNormalSchedulePointCount / kGpuRandNormalCdfTarget;
  auto get_x = [=](int idx) -> double { return x_pos_lower + step * idx; };
  base_schedule_points_.reserve(kGpuRandNormalSchedulePointCount);
  for (int i = 0; i < kGpuRandNormalSchedulePointCount; ++i) {
    const double integral = dist_.CDF(get_x(i + 1)) - dist_.CDF(get_x(i));
    int load = static_cast<int>(integral * factor);
    // Clamp to the valid per-device load range. Truncation in the
    // discretisation can push points slightly above 100 in extreme
    // configurations; capping here keeps SetLoadSet() inside its
    // documented [0..kScheduleIntervalMS] envelope.
    load = std::clamp(load, 0, kGpuMaxLoadPerDevice);
    base_schedule_points_.emplace_back(load);
  }
}

std::vector<int> GpuResourceManagerRandomNormal::ShuffledPoints() {
  std::vector<int> copy = base_schedule_points_;
  std::shuffle(copy.begin(), copy.end(), generator_);
  return copy;
}

}  // namespace gpu
