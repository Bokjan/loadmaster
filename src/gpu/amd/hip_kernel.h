#pragma once

#include "util/obfuscate.h"

namespace gpu::amd {

// HIP C++ source for the busy kernel; compiled at runtime by hipRTC for
// the actual target GPU. Mirrors the NVIDIA PTX busy kernel: each thread
// runs an LCG-style integer multiply-add loop and stores the final value
// to memory so it can't be optimized away, and every `mem_stride`
// iterations reads a word of the `-gm` block and writes it back changed
// (see gpu::MemSweep; mem_words = 0 turns that off).
//
// The plaintext source below is XOR-encoded at compile time; only the
// obfuscated bytes hit .rodata. AmdDevice::CompileKernel() decodes
// into a stack-local buffer just before handing it to hiprtcCreateProgram.
inline constexpr const char kBusyKernelHipSource[] = R"HIP(
extern "C" __global__ void busy(unsigned int *out, int loop_count, unsigned int *mem,
                                unsigned long long mem_offset, unsigned int mem_words,
                                unsigned int mem_base, int mem_stride) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    unsigned int threads = gridDim.x * blockDim.x;
    unsigned int val = static_cast<unsigned int>(tid) * 2654435761u + 1u;
    const unsigned int factor = 1664525u;
    const int chunk = mem_stride > 0 ? mem_stride : loop_count;
    unsigned int *win = mem + mem_offset;
    unsigned int idx = mem_base + static_cast<unsigned int>(tid);
    if (idx >= mem_words) idx -= mem_words;
    // Each word is loaded a chunk ahead of its store and the ALU chain
    // never waits on it, so the chunk's work hides the load's latency.
    unsigned int w = mem_words != 0u ? win[idx] : 0u;
    for (int i = 0; i < loop_count;) {
        const int end = i + (loop_count - i < chunk ? loop_count - i : chunk);
        for (; i < end; ++i) {
            val = val * factor + 1013904223u;
        }
        if (mem_words != 0u) {
            win[idx] = w ^ val;
            idx += threads;
            if (idx >= mem_words) idx -= mem_words;
            if (i < loop_count) w = win[idx];
        }
    }
    out[tid] = val;
}
)HIP";

inline constexpr std::uint32_t kBusyKernelHipKey = 0x4E2DA8B7u;

inline constexpr auto kBusyKernelHipEncoded =
    util::obfuscate::Encode(kBusyKernelHipSource, kBusyKernelHipKey);

}  // namespace gpu::amd
