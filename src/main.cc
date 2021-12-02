#include "cli/cli.h"
#include "global.h"
#include "runtime.h"
#include "util/log.h"

#include <signal.h>

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
  runtime.InitThreads();
  runtime.CreateWorkerThreads();
  runtime.JoinThreads();
}

void RegisterSignalHandler() {
  auto exit_handler = [](int signal) {
    LOG("signal %d captured, exit", signal);
    global::StopLoop();
  };
  signal(SIGINT, exit_handler);
  signal(SIGTERM, exit_handler);
}
