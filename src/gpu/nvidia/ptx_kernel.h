#pragma once

#include "util/obfuscate.h"

namespace gpu::nvidia {

// A minimal PTX kernel that performs `loop_count` 32-bit integer
// multiply-add operations per thread on a register-resident value. The
// final value is stored back to `out[tid]` so the compiler cannot
// dead-code-eliminate the loop.
//
// Signature (from the host's perspective):
//     extern "C" __global__ void busy(uint32_t *out, int loop_count)
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
    .param .u32 busy_param_1
)
{
    .reg .pred  %p<2>;
    .reg .b32   %r<10>;
    .reg .b64   %rd<6>;

    // %rd1 = out base ptr
    ld.param.u64    %rd1, [busy_param_0];
    cvta.to.global.u64 %rd2, %rd1;
    // %r1 = loop_count
    ld.param.u32    %r1, [busy_param_1];

    // tid = blockIdx.x * blockDim.x + threadIdx.x
    mov.u32         %r2, %ctaid.x;
    mov.u32         %r3, %ntid.x;
    mov.u32         %r4, %tid.x;
    mad.lo.s32      %r5, %r2, %r3, %r4;

    // %rd3 = &out[tid]
    mul.wide.u32    %rd4, %r5, 4;
    add.s64         %rd3, %rd2, %rd4;

    // val = tid * 0x9E3779B9 + 1  (some non-trivial seed)
    mul.lo.u32      %r6, %r5, -1640531527;
    add.u32         %r7, %r6, 1;

    // factor = 1664525 (Numerical Recipes LCG multiplier)
    mov.u32         %r8, 1664525;

    // i = 0
    mov.u32         %r9, 0;

loop_top:
    setp.ge.s32     %p1, %r9, %r1;
    @%p1 bra        loop_end;
    // val = val * factor + 1013904223
    mad.lo.u32      %r7, %r7, %r8, 1013904223;
    add.s32         %r9, %r9, 1;
    bra             loop_top;

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
