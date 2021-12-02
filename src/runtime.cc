#include "runtime.h"

#include "constants.h"
#include "cpu/master.h"
#include "options.h"
#include "util/log.h"

#include <unistd.h>

Runtime::Runtime(const Options &options) : options_(options) {
  long freq = sysconf(_SC_CLK_TCK);
  jiffy_ms_ = kMillisecondsPerSecond / freq;
}

void Runtime::InitThreads() { cpu::InitThreads(options_, *this); }

void Runtime::CreateWorkerThreads() { cpu::CreateWorkerThreads(*this); }

void Runtime::JoinThreads() {
  wg_.Wait();
}
