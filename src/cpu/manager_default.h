#pragma once

#include "manager.h"

namespace cpu {

class CpuResourceManagerDefault final : public CpuResourceManager {
 public:
  explicit CpuResourceManagerDefault(const Options &options);

 protected:
  void AdjustWorkerLoad(int cpu_load);
};

}
