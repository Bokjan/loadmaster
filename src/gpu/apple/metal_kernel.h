#pragma once

#include "util/obfuscate.h"

namespace gpu::apple {

// Metal Shading Language (MSL) source for the busy kernel. Same shape as
// the NVIDIA PTX / AMD HIP versions: every thread runs `loop_count`
// 32-bit integer multiply-adds in a register-resident value and stores
// the final value to out[tid] so the optimizer cannot dead-code-
// eliminate the loop.
//
// Signature (host-side dispatch):
//   buffer 0: device uint32_t *out                  // grid-sized scratch
//   buffer 1: constant uint32_t &loop_count         // per-launch counter
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
    uint tid                         [[thread_position_in_grid]])
{
    // Numerical-Recipes LCG: same constants as the PTX and HIP variants
    // so the three implementations exhibit similar instruction mixes.
    uint val = tid * 0x9E3779B9u + 1u;
    const uint factor = 1664525u;
    for (uint i = 0u; i < loop_count; ++i) {
        val = val * factor + 1013904223u;
    }
    out[tid] = val;
}
)MSL";

inline constexpr std::uint32_t kBusyKernelMslKey = 0xC1593E62u;

inline constexpr auto kBusyKernelMslEncoded =
    util::obfuscate::Encode(kBusyKernelMslSource, kBusyKernelMslKey);

}  // namespace gpu::apple
