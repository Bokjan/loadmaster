#pragma once

class Options;
class Runtime;

namespace cpu {

void InitThreads(const Options &options, Runtime &runtime);
void CreateWorkerThreads(Runtime &runtime);

}
