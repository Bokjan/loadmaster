#pragma once

#include <random>
#include <vector>

#include "manager.h"

#include "util/normal_dist.h"

namespace gpu {

// Random-normal scheduler: each device walks an independently-shuffled
// sequence of load points whose distribution approximates a truncated
// normal, with the time-average across one full
// `kGpuRandNormalSchedulePeriodMS` window equal to `-g <load>`.
//
// Per-device independence (rather than one shared schedule) so that
// boards in a multi-GPU host don't all spike or all idle in lockstep --
// the aggregate power draw stays smoother and the test more closely
// resembles a real mixed workload.
class GpuResourceManagerRandomNormal final : public GpuResourceManager {
 public:
  GpuResourceManagerRandomNormal(const core::Options &options,
                                 std::vector<std::pair<int, std::unique_ptr<GpuDevice>>> devices);

  bool Init() override;
  void Schedule(TimePoint time_point) override;

 private:
  // Per-device state: each device has its own shuffled sequence (so
  // devices don't fire simultaneously) and its own "next change" deadline.
  struct DeviceState {
    std::vector<int> schedule_points;
    int point_idx = 0;
    TimePoint last_change;  // default-constructed: epoch-zero -> first tick fires
  };

  std::mt19937 generator_;
  util::NormalDistribution dist_;
  std::vector<int> base_schedule_points_;  // un-shuffled template, derived once
  std::vector<DeviceState> states_;

  // Generate the unshuffled point template based on options_.GetGpuLoad().
  void GenerateBaseSchedulePoints();
  // Produce a freshly-shuffled copy of the base template.
  std::vector<int> ShuffledPoints();
  // Advance one device's schedule if its 10s deadline has elapsed.
  void AdvanceDevice(size_t worker_index, TimePoint time_point);
};

}  // namespace gpu
