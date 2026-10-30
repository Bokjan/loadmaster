#pragma once

#include "util/obfuscate.h"

namespace gpu::apple {

// Metal Shading Language (MSL) source for the busy kernel. Same shape as
// the NVIDIA PTX / AMD HIP versions: every thread runs `loop_count`
// 32-bit integer multiply-adds in a register-resident value and stores
// the final value to out[tid] so the optimizer cannot dead-code-
// eliminate the loop. Every `mem_stride` iterations it also reads a word
// of the `-gm` block and writes it back changed (see gpu::MemSweep;
// mem_words = 0 turns that off).
//
// Signature (host-side dispatch):
//   buffer 0: device uint32_t *out                  // grid-sized scratch
//   buffer 1: constant uint32_t &loop_count         // per-launch counter
//   buffer 2: device uint32_t *mem                  // `-gm` block
//   buffer 3: constant uint64_t &mem_offset         // window start, words
//   buffer 4: constant uint32_t &mem_words
//   buffer 5: constant uint32_t &mem_base
//   buffer 6: constant uint32_t &mem_stride
//
// Threadgroup geometry is set by the host to match
// kGpuKernelGridSize x kGpuKernelBlockSize threads, mirroring the CUDA /
// HIP launch.
//
// The plaintext source below is XOR-encoded at compile time; only the
// obfuscated bytes hit .rodata. AppleMetalDevice::Init() decodes into
// a stack-local buffer just before passing it to
// `MTLDevice newLibraryWithSource:`.
inline constexpr const char kBusyKernelMslSource[] = R"MSL(
#include <metal_stdlib>
using namespace metal;

kernel void busy_kernel(
    device uint *out                 [[buffer(0)]],
    constant uint &loop_count        [[buffer(1)]],
    device uint *mem                 [[buffer(2)]],
    constant ulong &mem_offset       [[buffer(3)]],
    constant uint &mem_words         [[buffer(4)]],
    constant uint &mem_base          [[buffer(5)]],
    constant uint &mem_stride        [[buffer(6)]],
    uint tid                         [[thread_position_in_grid]],
    uint threads                     [[threads_per_grid]])
{
    // Numerical-Recipes LCG: same constants as the PTX and HIP variants
    // so the three implementations exhibit similar instruction mixes.
    uint val = tid * 0x9E3779B9u + 1u;
    const uint factor = 1664525u;
    const uint chunk = mem_stride > 0u ? mem_stride : loop_count;
    device uint *win = mem + mem_offset;
    uint idx = mem_base + tid;
    if (idx >= mem_words) idx -= mem_words;
    // Each word is loaded a chunk ahead of its store and the ALU chain
    // never waits on it, so the chunk's work hides the load's latency.
    uint w = mem_words != 0u ? win[idx] : 0u;
    for (uint i = 0u; i < loop_count;) {
        const uint end = i + min(loop_count - i, chunk);
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
)MSL";

inline constexpr std::uint32_t kBusyKernelMslKey = 0xC1593E62u;

inline constexpr auto kBusyKernelMslEncoded =
    util::obfuscate::Encode(kBusyKernelMslSource, kBusyKernelMslKey);

}  // namespace gpu::apple
