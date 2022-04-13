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
  TimePoint last_schedule_;
  std::mt19937 generator_;
  
  void AdjustWorkerLoad(TimePoint time_point, int cpu_load) override;
  void GenerateSchedulePoints();
  void ShuffleSchedulePoints();
  void IncreasePointIndex();
};

}  // namespace cpu
