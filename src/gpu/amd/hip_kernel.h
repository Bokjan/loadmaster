#pragma once

#include "util/obfuscate.h"

namespace gpu::amd {

// HIP C++ source for the busy kernel; compiled at runtime by hipRTC for
// the actual target GPU. Mirrors the NVIDIA PTX busy kernel: each thread
// runs an LCG-style integer multiply-add loop and stores the final value
// to memory so it can't be optimized away.
//
// The plaintext source below is XOR-encoded at compile time; only the
// obfuscated bytes hit .rodata. AmdDevice::CompileKernel() decodes
// into a stack-local buffer just before handing it to hiprtcCreateProgram.
inline constexpr const char kBusyKernelHipSource[] = R"HIP(
extern "C" __global__ void busy(unsigned int *out, int loop_count) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    unsigned int val = static_cast<unsigned int>(tid) * 2654435761u + 1u;
    const unsigned int factor = 1664525u;
    for (int i = 0; i < loop_count; ++i) {
        val = val * factor + 1013904223u;
    }
    out[tid] = val;
}
)HIP";

inline constexpr std::uint32_t kBusyKernelHipKey = 0x4E2DA8B7u;

inline constexpr auto kBusyKernelHipEncoded =
    util::obfuscate::Encode(kBusyKernelHipSource, kBusyKernelHipKey);

}  // namespace gpu::amd
