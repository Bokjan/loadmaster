#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "resource_manager.h"

namespace core {

class Options;

using FnCreateResourceManager =
    std::function<std::unique_ptr<ResourceManager>(const Options &options)>;

// Top-level lifecycle orchestrator. Given a list of resource-manager
// factories (CPU / Memory / GPU / ...), `Run()` creates each manager,
// starts its workers, drives the scheduling main loop until a quit
// signal arrives, and shuts everything down cleanly.
//
// Signal handling is intentionally NOT installed here: the OS-level
// sigaction/std::signal call belongs to `main` (so the signal disposition
// is set before any worker thread exists). Runtime only consumes the
// SignalFlag that the handler writes into.
class Runtime final {
 public:
  // `creators` may contain `nullptr` entries; they are ignored. Each
  // factory that returns a non-null manager whose Init() succeeds is
  // driven for the rest of the run.
  Runtime(const Options &options, std::vector<FnCreateResourceManager> creators);

  // Run all 5 lifecycle stages back-to-back and return when a quit
  // signal (SIGINT/SIGTERM) has been observed and every worker thread
  // has been joined. Safe to call exactly once per Runtime instance.
  void Run();

 private:
  bool running_flag_;
  const Options &options_;
  std::vector<FnCreateResourceManager> creators_;
  std::vector<std::unique_ptr<ResourceManager>> managers_;

  // Signal-dispatch plumbing. The static table maps signal numbers to
  // member-function pointers; CreateSignalHandlerFunctors() turns those
  // into bound std::function entries we can iterate over each tick.
  //
  // Kept around in this form (rather than collapsed to direct switch())
  // so adding a new SIGUSRx hook stays mechanical.
  using FnSignalHandler = std::function<void()>;
  using FnMetaMetaHandler = void (Runtime::*)();
  using PairSignalHandler = std::pair<int, FnSignalHandler>;
  using PairMetaSignalHandler = std::pair<int, FnMetaMetaHandler>;
  std::vector<PairSignalHandler> signal_handlers_;
  static PairMetaSignalHandler meta_signal_handlers_[];

  // Lifecycle stages, invoked in order by Run(). Each one is a no-op
  // when there are no managers to drive.
  void Init();
  void CreateWorkers();
  void MainLoop();
  void StopWorkers();
  void JoinWorkers();

  void SigIntTerm();
  void SigUsr1();
  void SigUsr2();
  void DealSignals();
  void CreateSignalHandlerFunctors();

  static TimePoint NextSchedulingTime(TimePoint current_start);
};

}  // namespace core
