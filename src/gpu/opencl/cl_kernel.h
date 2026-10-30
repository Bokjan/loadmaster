#pragma once

#include "util/obfuscate.h"

namespace gpu::opencl {

// OpenCL C source for the busy kernel. Compiled at runtime by the
// installed ICD's compiler (clBuildProgram), so the same string works
// on Intel iGPU, AMD APU iGPU, AMD dGPU, NVIDIA, Mali, etc. Mirrors the
// NVIDIA PTX / AMD HIP / Intel SPIR-V busy kernels: each work-item runs
// an LCG-style 32-bit integer multiply-add loop and stores the final
// value back so the optimizer cannot eliminate the loop, and every
// `mem_stride` iterations reads a word of the `-gm` block and writes it
// back changed (see gpu::MemSweep; mem_words = 0 turns that off).
//
// The plaintext source below is XOR-encoded at compile time; only the
// obfuscated bytes hit .rodata. OpenClDevice::Init() decodes into a
// stack-local buffer just before passing it to clCreateProgramWithSource.
inline constexpr const char kBusyKernelClSource[] = R"CL(
__kernel void busy(__global uint *out, int loop_count, __global uint *mem, ulong mem_offset,
                   uint mem_words, uint mem_base, int mem_stride) {
    int tid = (int)get_global_id(0);
    uint threads = (uint)get_global_size(0);
    uint val = (uint)tid * 2654435761u + 1u;
    const uint factor = 1664525u;
    const int chunk = mem_stride > 0 ? mem_stride : loop_count;
    __global uint *win = mem + mem_offset;
    uint idx = mem_base + (uint)tid;
    if (idx >= mem_words) idx -= mem_words;
    // Each word is loaded a chunk ahead of its store and the ALU chain
    // never waits on it, so the chunk's work hides the load's latency.
    uint w = mem_words != 0u ? win[idx] : 0u;
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
)CL";

inline constexpr std::uint32_t kBusyKernelClKey = 0x9D6BAE17u;

inline constexpr auto kBusyKernelClEncoded =
    util::obfuscate::Encode(kBusyKernelClSource, kBusyKernelClKey);

}  // namespace gpu::opencl
