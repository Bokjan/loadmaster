#include "manager_random_normal.h"

#include "constants.h"
#include "options.h"
#include "util/log.h"

#include <random>

namespace cpu {

CpuResourceManagerRandomNormal::CpuResourceManagerRandomNormal(const Options &options)
    : CpuResourceManager(options), point_idx_(0), dist_(kCpuRandNormalMu, kCpuRandNormalSigma) {
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
  if (options_.cpu_count_ > 0) {
    count = options_.cpu_count_;
  } else {
    count = (max_load + (kCpuMaxLoadPerCore - 1)) / kCpuMaxLoadPerCore;
  }
  if (count > Count()) {
    LOG_ERROR("CPU load `%d` needs %d CPU, have %d", options_.cpu_load_, count, Count());
    return false;
  }
  if (count <= 0) {
    return false;
  }
  for (int i = 0; i < count; ++i) {
    CpuWorkerContext ctx(i, wg_);
    workers_.emplace_back(std::move(ctx));
  }
  return true;
}

void CpuResourceManagerRandomNormal::AdjustWorkerLoad(TimePoint time_point, int cpu_load) {
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(time_point - last_schedule_).count();
  if (elapsed_ms < kCpuRandNormalScheduleIntervalMS) {
    return;
  }

  if (point_idx_ == 0) {
    ShuffleSchedulePoints();
  }

  int avg_load = schedule_points_[point_idx_] / workers_.size();
  if (cpu_load > kCpuPauseLoopLoadPercentage * Count()) {
    avg_load = 0;
  }

  LOG_DEBUG("idx=%d, total_target=%d, avg_target=%d", point_idx_, schedule_points_[point_idx_],
            avg_load);

  for (auto &ctx : workers_) {
    ctx.load_set_ = avg_load;
  }

  last_schedule_ = time_point;
  IncreasePointIndex();
}

void CpuResourceManagerRandomNormal::GenerateSchedulePoints() {
  schedule_points_.clear();
  double x_pos_upper = dist_.FindXAxisPositionCDF(kCpuRandNormalCdfTarget);
  double x_pos_lower = kCpuRandNormalMu - (x_pos_upper - kCpuRandNormalMu);
  double step = (x_pos_upper - x_pos_lower) / kCpuRandNormalSchedulePointCount;
  double factor = options_.cpu_load_ * kCpuRandNormalSchedulePointCount / kCpuRandNormalCdfTarget;
  // LOG_DEBUG("lower=%lf, upper=%lf, step=%lf, factor=%lf", x_pos_lower, x_pos_upper, step, factor);
  auto get_x = [=](int idx) -> double { return x_pos_lower + step * idx; };
  for (int i = 0; i < kCpuRandNormalSchedulePointCount; ++i) {
    double integral = dist_.CDF(get_x(i + 1)) - dist_.CDF(get_x(i));
    int load = static_cast<int>(integral * factor);
    schedule_points_.emplace_back(load);
    // LOG_DEBUG("i=%d, val=%d", i, load);
  }
}

void CpuResourceManagerRandomNormal::ShuffleSchedulePoints() {
  int count = static_cast<int>(schedule_points_.size());
  std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<> udist(0, count - 1);
  for (int i = 0; i < count; ++i) {
    int x = udist(gen);
    int y = udist(gen);
    std::swap(schedule_points_[x], schedule_points_[y]);
  }
}

void CpuResourceManagerRandomNormal::IncreasePointIndex() {
  ++point_idx_;
  if (static_cast<size_t>(point_idx_) == schedule_points_.size()) {
    point_idx_ = 0;
  }
}

}  // namespace cpu
