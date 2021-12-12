#pragma once

#include "manager.h"

namespace cpu {

class CpuResourceManagerDefault final : public CpuResourceManager {
 public:
  explicit CpuResourceManagerDefault(const Options &options);

 private:
  void AdjustWorkerLoad(TimePoint time_point, int cpu_load) override;
};

}
