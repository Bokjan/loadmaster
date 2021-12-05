#pragma once

#include <memory>

class Options;
class ResourceManager;

namespace memory {

std::unique_ptr<ResourceManager> CreateResourceManager(const Options &options);

}
