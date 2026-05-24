#pragma once

#include "core/resource_manager.h"

namespace memory {

class MemoryResourceManager : public core::ResourceManager {
 public:
  virtual bool Init() override;
  virtual void CreateWorkerThreads() override final;
  virtual void RequestWorkerThreadsStop() override final;
  virtual void JoinWorkerThreads() override final;

 protected:
  explicit MemoryResourceManager(const core::Options &options);
};

}  // namespace memory
