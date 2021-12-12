#pragma once

#include "manager.h"

#include "util/normal_dist.h"

#include <vector>

namespace cpu {

class CpuResourceManagerRandomNormal final : public CpuResourceManager {
 public:
  explicit CpuResourceManagerRandomNormal(const Options &options);
  virtual bool Init() override;

 private:
  int point_idx_;
  std::vector<int> schedule_points_;
  util::NormalDistribution dist_;
  TimePoint last_schedule_;
  void AdjustWorkerLoad(TimePoint time_point, int cpu_load) override;
  void GenerateSchedulePoints();
  void ShuffleSchedulePoints();
  void IncreasePointIndex();
  bool CpuLoadThresholdGuard(int cpu_load);
};

}  // namespace cpu
