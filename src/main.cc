#include <csignal>

#include "cli/cli.h"
#include "core/runtime.h"
#include "core/signal_flag.h"
#include "util/log.h"

static void Work(const core::Options &options);
static void RegisterSignalHandler();

int main(int argc, const char *argv[]) {
  core::Options options;
  cli::ParseCommandLineArguments(options, argc, argv);
  RegisterSignalHandler();
  Work(options);
  LOG_INFO("main() finished");
}

static void Work(const core::Options &options) {
  core::Runtime runtime(options);
  runtime.Init();
  runtime.CreateWorkers();
  runtime.MainLoop();
  runtime.StopWorkers();
  runtime.JoinWorkers();
}

static void RegisterSignalHandler() {
  static int concerned_signals[] = {
      SIGINT,
      SIGTERM,
      SIGUSR1,
      SIGUSR2,
  };
  auto sf_writer = [](int signal) {
    LOG_INFO("signal %d captured", signal);
    core::SignalFlag::Get().Set(signal);
  };
  for (auto s : concerned_signals) {
    signal(s, sf_writer);
  }
}
