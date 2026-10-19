#include "manager_random_normal.h"

#include <algorithm>
#include <utility>

#include "constants.h"

#include "core/options.h"
#include "util/log.h"
#include "util/normal_schedule.h"

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
  // The first tick fires immediately. (Not via a default-constructed
  // last_change: steady_clock's epoch is typically boot time, so "long
  // ago" isn't guaranteed.)
  if (state.started) {
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(time_point - state.last_change)
            .count();
    if (elapsed_ms < kGpuRandNormalScheduleIntervalMS) {
      return;
    }
  }
  // Wrapped around: re-shuffle so the next 10-min window picks a new
  // ordering of the same values.
  if (state.point_idx == 0 && state.started) {
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
  state.started = true;
}

void GpuResourceManagerRandomNormal::GenerateBaseSchedulePoints() {
  // Same construction as the CPU rand_normal scheduler, but every point is
  // capped at 100% of the device; the excess clipped off the peaks is
  // redistributed so the period average still equals options_.GetGpuLoad().
  base_schedule_points_ =
      util::BuildNormalSchedulePoints(dist_, kGpuRandNormalSchedulePointCount,
                                      kGpuRandNormalCdfTarget, options_.GetGpuLoad(),
                                      kGpuMaxLoadPerDevice);
}

std::vector<int> GpuResourceManagerRandomNormal::ShuffledPoints() {
  std::vector<int> copy = base_schedule_points_;
  std::shuffle(copy.begin(), copy.end(), generator_);
  return copy;
}

}  // namespace gpu
