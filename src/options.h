#pragma once

#include "constants.h"

class Options {
 public:
  enum class ScheduleAlgorithm : int { kDefault, kRandomNormal };

  int cpu_load_;
  int cpu_count_;
  ScheduleAlgorithm cpu_algorithm_;
  int memory_;

  Options();
};
