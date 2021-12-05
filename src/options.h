#pragma once

#include "constants.h"

class Options {
 public:
  int cpu_load_;
  int cpu_count_;
  Options() : cpu_load_(kDefaultCpuLoad), cpu_count_(0) {}
};
