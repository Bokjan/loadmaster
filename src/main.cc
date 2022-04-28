#include <csignal>

#include "cli/cli.h"
#include "core/runtime.h"
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
  auto exit_handler = [](int signal) {
    LOG_INFO("signal %d captured, exit", signal);
    core::RunningFlag::Get().Stop();
  };
  signal(SIGINT, exit_handler);
  signal(SIGTERM, exit_handler);
}
