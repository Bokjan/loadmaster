#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>

#include "cli/cli.h"
#include "core/platform.h"
#include "core/runtime.h"
#include "core/signal_flag.h"
#include "cpu/factory.h"
#include "gpu/factory.h"
#include "memory/factory.h"
#include "util/log.h"

namespace {

#if !IS_WINDOWS
void HandleSignal(int signal) {
  // Async-signal-safe path: write to atomic bitmap only. Don't log here.
  core::SignalFlag::Get().Set(signal);
}

void RegisterSignalHandlers() {
  static const int kSignals[] = {SIGINT, SIGTERM, SIGUSR1, SIGUSR2};
  // sigaction rather than signal(): well-defined, persistent disposition.
  // SA_RESTART is left off for good measure, but nothing relies on EINTR:
  // the main loop and the workers poll their flags at least every
  // scheduling tick (kScheduleIntervalMS) and std::this_thread::sleep_*
  // resumes after EINTR anyway.
  struct sigaction sa{};
  sa.sa_handler = HandleSignal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  for (int s : kSignals) {
    if (sigaction(s, &sa, nullptr) != 0) {
      LOG_WARN("sigaction failed for signal %d: %s", s, std::strerror(errno));
    }
  }
}

void NotifyShutdownComplete() {}
#else
// Signalled by main() once the runtime has torn everything down, so a
// console close / logoff / shutdown event can wait for cleanup instead of
// being killed mid-teardown.
HANDLE g_shutdown_complete = nullptr;

BOOL WINAPI HandleConsoleCtrl(DWORD ctrl_type) {
  // Runs on a dedicated thread created by the console host, so blocking
  // here is allowed.
  switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
      core::SignalFlag::Get().Set(SIGINT);
      return TRUE;
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      core::SignalFlag::Get().Set(SIGTERM);
      // Returning from the handler for these events terminates the
      // process (the OS grants ~5 s), so hold on until main() reports
      // that teardown -- GPU contexts, memory release -- has finished.
      if (g_shutdown_complete != nullptr) {
        ::WaitForSingleObject(g_shutdown_complete, 4500);
      }
      return TRUE;
    default:
      return FALSE;
  }
}

void RegisterSignalHandlers() {
  // SetConsoleCtrlHandler instead of std::signal: CRT signal() handlers
  // are one-shot (a second Ctrl-C during shutdown killed the process),
  // SIGTERM is never raised on Windows, and Ctrl-Break / console close
  // were not handled at all.
  g_shutdown_complete = ::CreateEventA(nullptr, TRUE, FALSE, nullptr);
  if (!::SetConsoleCtrlHandler(HandleConsoleCtrl, TRUE)) {
    LOG_WARN("SetConsoleCtrlHandler failed: %lu", static_cast<unsigned long>(::GetLastError()));
  }
}

void NotifyShutdownComplete() {
  if (g_shutdown_complete != nullptr) {
    ::SetEvent(g_shutdown_complete);
  }
}
#endif

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

  bool ran = false;
  {
    core::Runtime runtime(options, std::move(creators));
    ran = runtime.Run();
  }  // managers (GPU contexts, memory blocks) are destroyed here
  NotifyShutdownComplete();

  LOG_INFO("main() finished");
  return ran ? EXIT_SUCCESS : EXIT_FAILURE;
}
