#include <csignal>
#include <cstdlib>
#include <cstring>

#include "cli/cli.h"
#include "core/runtime.h"
#include "core/signal_flag.h"
#include "util/log.h"

namespace {

void Work(const core::Options &options) {
  core::Runtime runtime(options);
  runtime.Init();
  runtime.CreateWorkers();
  runtime.MainLoop();
  runtime.StopWorkers();
  runtime.JoinWorkers();
}

void HandleSignal(int signal) {
  // Async-signal-safe path: write to atomic bitmap only. Don't log here.
  core::SignalFlag::Get().Set(signal);
}

void RegisterSignalHandlers() {
  static const int kSignals[] = {
      SIGINT,
      SIGTERM,
#if !IS_WINDOWS
      SIGUSR1,
      SIGUSR2,
#endif
  };
#if !IS_WINDOWS
  // Use sigaction for well-defined POSIX behavior (signal() semantics vary).
  struct sigaction sa {};
  sa.sa_handler = HandleSignal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;  // do NOT set SA_RESTART; we want syscalls to be interruptible
  for (int s : kSignals) {
    if (sigaction(s, &sa, nullptr) != 0) {
      LOG_WARN("sigaction failed for signal %d: %s", s, std::strerror(errno));
    }
  }
#else
  for (int s : kSignals) {
    std::signal(s, HandleSignal);
  }
#endif
}

}  // namespace

int main(int argc, const char *argv[]) {
  core::Options options;
  const auto parse = cli::ParseCommandLineArguments(options, argc, argv);
  if (parse.should_exit) {
    return parse.exit_code;
  }
  RegisterSignalHandlers();
  Work(options);
  LOG_INFO("main() finished");
  return EXIT_SUCCESS;
}
