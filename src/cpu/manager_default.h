#pragma once

#include "cpu/manager.h"

namespace cpu {

class CpuResourceManagerDefault final : public CpuResourceManager {
 public:
  explicit CpuResourceManagerDefault(const Options &options);
  virtual bool Init() override;

 private:
  void AdjustWorkerLoad(TimePoint time_point, int system_load) override;
};

}
