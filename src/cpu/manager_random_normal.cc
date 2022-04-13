#include "manager_random_normal.h"

#include "constants.h"
#include "options.h"
#include "util/log.h"

namespace cpu {

CpuResourceManagerRandomNormal::CpuResourceManagerRandomNormal(const Options &options)
    : CpuResourceManager(options),
      point_idx_(0),
      dist_(kCpuRandNormalMu, kCpuRandNormalSigma),
      generator_(std::random_device{}()) {
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
  if (options_.CpuCount() > 0) {
    count = options_.CpuCount();
  } else {
    count = (max_load + (kCpuMaxLoadPerCore - 1)) / kCpuMaxLoadPerCore;
  }
  if (count > Count()) {
    LOG_ERROR("CPU load `%d` needs %d CPU, have %d", options_.CpuLoad(), count, Count());
    return false;
  }
  if (count <= 0) {
    return false;
  }
  for (int i = 0; i < count; ++i) {
    CpuWorkerContext ctx(i, wg_, base_loop_count_);
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

  int core_target = 0;

  do {
    auto load_demand = schedule_points_[point_idx_] - cpu_load;
    if (load_demand <= 0) {
      break;
    }
    core_target = load_demand / static_cast<int>(workers_.size());
    LOG_TRACE("idx=%d, total_target=%d, load_demand=%d, core_target=%d", point_idx_,
              schedule_points_[point_idx_], core_target);
  } while (false);

  for (auto &ctx : workers_) {
    ctx.SetLoadSet(core_target);
  }

  last_schedule_ = time_point;
  IncreasePointIndex();
}

void CpuResourceManagerRandomNormal::GenerateSchedulePoints() {
  point_idx_ = 0;
  schedule_points_.clear();
  double x_pos_upper = dist_.FindXAxisPositionCDF(kCpuRandNormalCdfTarget);
  double x_pos_lower = dist_.GetMean() - (x_pos_upper - dist_.GetMean());
  double step = (x_pos_upper - x_pos_lower) / kCpuRandNormalSchedulePointCount;
  double factor = options_.CpuLoad() * kCpuRandNormalSchedulePointCount / kCpuRandNormalCdfTarget;
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

}  // namespace cpu
