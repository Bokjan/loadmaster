#pragma once

#include "util/obfuscate.h"

namespace gpu::nvidia {

// A minimal PTX kernel that performs `loop_count` 32-bit integer
// multiply-add operations per thread on a register-resident value. The
// final value is stored back to `out[tid]` so the compiler cannot
// dead-code-eliminate the loop. Every `mem_stride` iterations it also reads
// a word of the `-gm` block and writes it back changed (see gpu::MemSweep
// for the arguments); mem_words = 0 turns that off.
//
// Signature (from the host's perspective):
//     extern "C" __global__ void busy(uint32_t *out, int loop_count,
//                                     uint32_t *mem, uint64_t mem_offset,
//                                     uint32_t mem_words, uint32_t mem_base,
//                                     int mem_stride)
//
// Compatibility: PTX is forward-compatible thanks to the driver-side JIT;
// targeting sm_30 covers virtually every supported GPU. The driver will
// transparently re-compile the PTX for newer architectures.
//
// The plaintext source below is encoded at compile time via
// util::obfuscate::Encode; only the obfuscated byte array hits the
// .rodata segment. NvidiaDevice::Init() decodes it into a stack-local
// buffer just before handing it to cuModuleLoadData. See
// util/obfuscate.h for the rationale and the build-time toggle.
inline constexpr const char kBusyKernelPtxSource[] = R"PTX(
//
// Generated for loadmaster: integer busy-loop with stable side effect.
//
.version 6.0
.target sm_30
.address_size 64

.visible .entry busy(
    .param .u64 busy_param_0,
    .param .u32 busy_param_1,
    .param .u64 busy_param_2,
    .param .u64 busy_param_3,
    .param .u32 busy_param_4,
    .param .u32 busy_param_5,
    .param .u32 busy_param_6
)
{
    .reg .pred  %p<7>;
    .reg .b32   %r<20>;
    .reg .b64   %rd<11>;

    // %rd1 = out base ptr
    ld.param.u64    %rd1, [busy_param_0];
    cvta.to.global.u64 %rd2, %rd1;
    // %r1 = loop_count
    ld.param.u32    %r1, [busy_param_1];
    // %rd6 = window base ptr = mem + 4 * mem_offset, %r10 = mem_words,
    // %r11 = mem_base, %r12 = mem_stride
    ld.param.u64    %rd5, [busy_param_2];
    cvta.to.global.u64 %rd9, %rd5;
    ld.param.u64    %rd10, [busy_param_3];
    shl.b64         %rd10, %rd10, 2;
    add.s64         %rd6, %rd9, %rd10;
    ld.param.u32    %r10, [busy_param_4];
    ld.param.u32    %r11, [busy_param_5];
    ld.param.u32    %r12, [busy_param_6];

    // tid = blockIdx.x * blockDim.x + threadIdx.x
    mov.u32         %r2, %ctaid.x;
    mov.u32         %r3, %ntid.x;
    mov.u32         %r4, %tid.x;
    mad.lo.s32      %r5, %r2, %r3, %r4;
    // %r14 = threads in the grid = gridDim.x * blockDim.x
    mov.u32         %r13, %nctaid.x;
    mul.lo.u32      %r14, %r13, %r3;

    // %rd3 = &out[tid]
    mul.wide.u32    %rd4, %r5, 4;
    add.s64         %rd3, %rd2, %rd4;

    // val = tid * 0x9E3779B9 + 1  (some non-trivial seed)
    mul.lo.u32      %r6, %r5, -1640531527;
    add.u32         %r7, %r6, 1;

    // factor = 1664525 (Numerical Recipes LCG multiplier)
    mov.u32         %r8, 1664525;

    // mem_stride < 1: a single chunk of loop_count iterations
    setp.lt.s32     %p2, %r12, 1;
    selp.b32        %r12, %r1, %r12, %p2;

    // idx = (mem_base + tid) wrapped into [0, mem_words)
    add.u32         %r15, %r11, %r5;
    setp.ge.u32     %p3, %r15, %r10;
    @%p3 sub.u32    %r15, %r15, %r10;

    // %p5 = no block. Otherwise %r17 = win[idx]: each word is loaded a
    // chunk ahead of its store and the ALU chain never waits on it, so the
    // chunk's work hides the load's latency.
    mov.u32         %r17, 0;
    setp.eq.u32     %p5, %r10, 0;
    @%p5 bra        loop_start;
    mul.wide.u32    %rd7, %r15, 4;
    add.s64         %rd8, %rd6, %rd7;
    ld.global.u32   %r17, [%rd8];

loop_start:
    // i = 0
    mov.u32         %r9, 0;

chunk_top:
    setp.ge.s32     %p1, %r9, %r1;
    @%p1 bra        loop_end;
    // end = i + min(loop_count - i, mem_stride)
    sub.s32         %r16, %r1, %r9;
    min.s32         %r16, %r16, %r12;
    add.s32         %r16, %r9, %r16;

loop_top:
    // val = val * factor + 1013904223
    mad.lo.u32      %r7, %r7, %r8, 1013904223;
    add.s32         %r9, %r9, 1;
    setp.lt.s32     %p4, %r9, %r16;
    @%p4 bra        loop_top;

    // Write the loaded word back changed, unless there is no block.
    @%p5 bra        chunk_top;
    mul.wide.u32    %rd7, %r15, 4;
    add.s64         %rd8, %rd6, %rd7;
    xor.b32         %r18, %r17, %r7;
    st.global.u32   [%rd8], %r18;
    // idx = (idx + threads) wrapped into [0, mem_words)
    add.u32         %r15, %r15, %r14;
    setp.ge.u32     %p3, %r15, %r10;
    @%p3 sub.u32    %r15, %r15, %r10;
    // Load the next word, unless that was the last chunk.
    setp.ge.s32     %p6, %r9, %r1;
    @%p6 bra        chunk_top;
    mul.wide.u32    %rd7, %r15, 4;
    add.s64         %rd8, %rd6, %rd7;
    ld.global.u32   %r17, [%rd8];
    bra             chunk_top;

loop_end:
    st.global.u32   [%rd3], %r7;
    ret;
}
)PTX";

// Per-backend XOR key. Picked at random; uniqueness across backends
// means a single recovered key doesn't decode the others.
inline constexpr std::uint32_t kBusyKernelPtxKey = 0x7A3F91C5u;

inline constexpr auto kBusyKernelPtxEncoded =
    util::obfuscate::Encode(kBusyKernelPtxSource, kBusyKernelPtxKey);

}  // namespace gpu::nvidia
