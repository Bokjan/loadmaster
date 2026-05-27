#pragma once

namespace gpu::opencl {

// OpenCL C source for the busy kernel. Compiled at runtime by the
// installed ICD's compiler (clBuildProgram), so the same string works
// on Intel iGPU, AMD APU iGPU, AMD dGPU, NVIDIA, Mali, etc. Mirrors the
// NVIDIA PTX / AMD HIP / Intel SPIR-V busy kernels: each work-item runs
// an LCG-style 32-bit integer multiply-add loop and stores the final
// value back so the optimizer cannot eliminate the loop.
inline constexpr const char *kBusyKernelClSrc = R"CL(
__kernel void busy(__global uint *out, int loop_count) {
    int tid = (int)get_global_id(0);
    uint val = (uint)tid * 2654435761u + 1u;
    const uint factor = 1664525u;
    for (int i = 0; i < loop_count; ++i) {
        val = val * factor + 1013904223u;
    }
    out[tid] = val;
}
)CL";

}  // namespace gpu::opencl
