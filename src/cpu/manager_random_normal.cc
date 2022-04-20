#include "cpu/manager_random_normal.h"

#include <algorithm>

#include "constants.h"
#include "core/options.h"
#include "util/log.h"

namespace cpu {

CpuResourceManagerRandomNormal::CpuResourceManagerRandomNormal(const Options &options)
    : CpuResourceManager(options),
      point_idx_(0),
      dist_(kCpuRandNormalMu, kCpuRandNormalSigma),
      generator_(std::random_device{}()),
      load_target_(0) {
  GenerateSchedulePoints();
}

bool CpuResourceManagerRandomNormal::Init() {
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
  if (count > Count()) {
    LOG_ERROR("CPU load `%d` needs %d CPU, have %d", options_.GetCpuLoad(), count, Count());
    return false;
  }
  if (count <= 0) {
    return false;
  }
  return this->ConstructWorkerThreads(count);
}

void CpuResourceManagerRandomNormal::AdjustWorkerLoad(TimePoint time_point, int system_load) {
  this->UpdateLoadTarget(time_point);
  auto load_demand = this->CalculateLoadDemand(load_target_);
  load_demand = std::clamp(load_demand, 0, load_target_);
  LOG_TRACE("load_target_=%d, avg_proc_load=%d, avg_sys_load=%d, load_demand=%d", load_target_,
            this->GetProcessAverageLoad(), this->GetSystemAverageLoad(), load_demand);
  this->SetWorkerLoadWithTotalLoad(load_demand);
}

void CpuResourceManagerRandomNormal::GenerateSchedulePoints() {
  point_idx_ = 0;
  schedule_points_.clear();
  double x_pos_upper = dist_.FindXAxisPositionCDF(kCpuRandNormalCdfTarget);
  double x_pos_lower = dist_.GetMean() - (x_pos_upper - dist_.GetMean());
  double step = (x_pos_upper - x_pos_lower) / kCpuRandNormalSchedulePointCount;
  double factor =
      options_.GetCpuLoad() * kCpuRandNormalSchedulePointCount / kCpuRandNormalCdfTarget;
  auto get_x = [=](int idx) -> double { return x_pos_lower + step * idx; };
  for (int i = 0; i < kCpuRandNormalSchedulePointCount; ++i) {
    double integral = dist_.CDF(get_x(i + 1)) - dist_.CDF(get_x(i));
    int load = static_cast<int>(integral * factor);
    schedule_points_.emplace_back(load);
  }
}

void CpuResourceManagerRandomNormal::ShuffleSchedulePoints() {
  int count = static_cast<int>(schedule_points_.size());
  std::uniform_int_distribution<> udist(0, count - 1);
  for (int i = 0; i < count; ++i) {
    int x = udist(generator_);
    int y = udist(generator_);
    std::swap(schedule_points_[x], schedule_points_[y]);
  }
}

void CpuResourceManagerRandomNormal::IncreasePointIndex() {
  ++point_idx_;
  if (point_idx_ == schedule_points_.size()) {
    point_idx_ = 0;
  }
}

void CpuResourceManagerRandomNormal::UpdateLoadTarget(TimePoint time_point) {
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(time_point - last_load_target_change_)
          .count();
  if (elapsed_ms < kCpuRandNormalScheduleIntervalMS) {
    return;
  }
  if (point_idx_ == 0) {
    ShuffleSchedulePoints();
  }

  load_target_ = schedule_points_[point_idx_];
  LOG_DEBUG("point_idx_=%d, load_target_=%d", point_idx_, load_target_);

  IncreasePointIndex();
  last_load_target_change_ = time_point;
}

}  // namespace cpu
