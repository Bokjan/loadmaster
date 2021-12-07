#include "cli/cli.h"
#include "global.h"
#include "runtime.h"
#include "util/log.h"

#include <csignal>

void Work(const Options &options);
void RegisterSignalHandler();

int main(int argc, const char *argv[]) {
  Options options;
  ParseCommandLineArguments(options, argc, argv);
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
    global::StopLoop();
  };
  signal(SIGINT, exit_handler);
  signal(SIGTERM, exit_handler);
}
