#include <csignal>

#include "cli/cli.h"
#include "core/runtime.h"
#include "util/log.h"

void Work(const Options &options);
void RegisterSignalHandler();

int main(int argc, const char *argv[]) {
  Options options;
  cli::ParseCommandLineArguments(options, argc, argv);
  RegisterSignalHandler();
  Work(options);
}

void Work(const Options &options) {
  Runtime runtime(options);
  runtime.Init();
  runtime.CreateWorkers();
  runtime.MainLoop();
  runtime.JoinWorkers();
}

void RegisterSignalHandler() {
  auto exit_handler = [](int signal) {
    LOG_INFO("signal %d captured, exit", signal);
    RunningFlag::Get().Stop();
  };
  signal(SIGINT, exit_handler);
  signal(SIGTERM, exit_handler);
}
