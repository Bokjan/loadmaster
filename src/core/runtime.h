#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "resource_manager.h"

#include "util/proc_stat.h"

namespace core {

using FnCreateResourceManager =
    std::function<std::unique_ptr<ResourceManager>(const Options &options)>;

class Options;

class Runtime final {
 public:
  explicit Runtime(const Options &options);

  void Init();
  void CreateWorkers();
  void MainLoop();
  void StopWorkers();
  void JoinWorkers();

 private:
  bool running_flag_;
  const Options &options_;
  std::vector<std::unique_ptr<ResourceManager>> managers_;

  using FnSignalHandler = std::function<void()>;
  using FnMetaMetaHandler = void (Runtime::*)();
  using PairSignalHandler = std::pair<int, FnSignalHandler>;
  using PairMetaSignalHandler = std::pair<int, FnMetaMetaHandler>;
  std::vector<PairSignalHandler> signal_handlers_;
  static PairMetaSignalHandler meta_signal_handlers_[];

  void SigIntTerm();
  void SigUsr1();
  void SigUsr2();
  void DealSignals();
  void CreateSignalHandlerFunctors();

  TimePoint NextSchedulingTime(TimePoint current_start);
};

}  // namespace core
