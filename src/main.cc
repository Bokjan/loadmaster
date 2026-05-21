#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>

#include "cli/cli.h"
#include "core/constants.h"
#include "core/runtime.h"
#include "core/signal_flag.h"
#include "cpu/factory.h"
#include "gpu/factory.h"
#include "memory/factory.h"
#include "util/log.h"

namespace {

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

  // Resource-manager factories, in the order they should be created and
  // scheduled. Each factory may return nullptr to mean "this module is
  // not applicable for the current options"; Runtime tolerates that.
  std::vector<core::FnCreateResourceManager> creators = {
      cpu::CreateResourceManager,
      memory::CreateResourceManager,
      gpu::CreateResourceManager,
  };

  core::Runtime runtime(options, std::move(creators));
  runtime.Run();

  LOG_INFO("main() finished");
  return EXIT_SUCCESS;
}
