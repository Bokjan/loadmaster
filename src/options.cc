#include "options.h"

#include "cpu/cpu.h"

Options::Options()
    : cpu_load_(kDefaultCpuLoad),
      cpu_count_(0),
      cpu_algorithm_(ScheduleAlgorithm::kUniform),
      memory_(kDefaultMemoryLoadMiB) {}
