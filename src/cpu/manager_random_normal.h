#pragma once

#include "manager.h"

#include "util/normal_dist.h"

#include <vector>
#include <random>

namespace cpu {

class CpuResourceManagerRandomNormal final : public CpuResourceManager {
 public:
  explicit CpuResourceManagerRandomNormal(const Options &options);
  virtual bool Init() override;

 private:
  size_t point_idx_;
  std::vector<int> schedule_points_;
  util::NormalDistribution dist_;
  std::mt19937 generator_;
  int load_target_;
  TimePoint last_load_target_change_;
  
  void AdjustWorkerLoad(TimePoint time_point, int system_load) override;

  void UpdateLoadTarget(TimePoint time_point);
  void GenerateSchedulePoints();
  void ShuffleSchedulePoints();
  void IncreasePointIndex();
};

}  // namespace cpu
