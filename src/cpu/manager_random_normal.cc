#include "manager_random_normal.h"

#include <algorithm>

#include "constants.h"

#include "core/options.h"
#include "util/log.h"
#include "util/normal_schedule.h"

namespace cpu {

CpuResourceManagerRandomNormal::CpuResourceManagerRandomNormal(const core::Options &options)
    : CpuResourceManager(options),
      point_idx_(0),
      dist_(kCpuRandNormalMu, kCpuRandNormalSigma),
      generator_(std::random_device{}()),
      load_target_(0) {
  GenerateSchedulePoints();
}

bool CpuResourceManagerRandomNormal::Init() {
  if (options_.GetCpuLoad() <= 0) {
    LOG_INFO("CPU module disabled (-l 0)");
    return false;
  }
  // Find max load in normal distribution
  int max_load = 0;
  for (const auto load : schedule_points_) {
    if (load > max_load) {
      max_load = load;
    }
  }
  // Determine how many threads should be used
  int count;
  if (options_.GetCpuCount() > 0) {
    count = options_.GetCpuCount();
  } else {
    count = (max_load + (kCpuMaxLoadPerCore - 1)) / kCpuMaxLoadPerCore;
  }
  if (count <= 0) {
    return false;
  }
  if (count > CoreCount()) {
    LOG_ERROR("rand_normal peak load=%d needs %d CPU cores, have %d (avg target=%d)", max_load,
              count, CoreCount(), options_.GetCpuLoad());
    return false;
  }
  if (max_load > count * kCpuMaxLoadPerCore) {
    // Explicit -c too small for the peaks: they would be silently capped
    // and the period average would fall short of -l.
    LOG_ERROR("rand_normal peak load=%d is unreachable with -c %d (avg target=%d)", max_load,
              count, options_.GetCpuLoad());
    return false;
  }
  // Shuffle once before the first scheduling tick so the first 10-minute
  // period is also random (otherwise the points would walk a non-random
  // monotonic-ish sequence on cold start).
  ShuffleSchedulePoints();
  return this->ConstructWorkerThreads(count);
}

void CpuResourceManagerRandomNormal::AdjustWorkerLoad(TimePoint time_point,
                                                      [[maybe_unused]] int system_load) {
  this->UpdateLoadTarget(time_point);
  auto load_demand = this->CalculateLoadDemand(load_target_);
  LOG_TRACE("load_target_=%d, avg_proc_load=%.1f, avg_sys_load=%.1f, load_demand=%d", load_target_,
            this->GetProcessAverageLoad(), this->GetSystemAverageLoad(), load_demand);
  this->SetWorkerLoadWithTotalLoad(load_demand);
}

void CpuResourceManagerRandomNormal::GenerateSchedulePoints() {
  point_idx_ = 0;
  // No per-point cap: Init() sizes the worker pool from the peak point.
  schedule_points_ = util::BuildNormalSchedulePoints(
      dist_, kCpuRandNormalSchedulePointCount, kCpuRandNormalCdfTarget, options_.GetCpuLoad());
}

void CpuResourceManagerRandomNormal::ShuffleSchedulePoints() {
  std::shuffle(schedule_points_.begin(), schedule_points_.end(), generator_);
}

void CpuResourceManagerRandomNormal::IncreasePointIndex() {
  ++point_idx_;
  if (point_idx_ == static_cast<int>(schedule_points_.size())) {
    point_idx_ = 0;
  }
}

void CpuResourceManagerRandomNormal::UpdateLoadTarget(TimePoint time_point) {
  if (load_target_started_) {
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                time_point - last_load_target_change_)
                                .count();
    if (elapsed_ms < kCpuRandNormalScheduleIntervalMS) {
      return;
    }
  }
  if (point_idx_ == 0) {
    ShuffleSchedulePoints();
  }

  load_target_ = schedule_points_[point_idx_];
  LOG_DEBUG("point_idx_=%d, load_target_=%d", point_idx_, load_target_);

  IncreasePointIndex();
  last_load_target_change_ = time_point;
  load_target_started_ = true;
}

}  // namespace cpu
