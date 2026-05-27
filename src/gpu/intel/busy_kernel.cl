// loadmaster busy kernel for the Intel (Level Zero) backend.
//
// Compiled offline into SPIR-V, then embedded into the binary at build
// time (see src/gpu/intel/spirv_kernel.h). Mirrors the NVIDIA PTX and
// AMD HIP busy kernels: each work-item runs an LCG-style 32-bit
// integer multiply-add loop and stores the final value back to a
// scratch buffer so the optimizer cannot eliminate the loop.
//
// Recommended build commands (run on any Linux box with the Intel
// compute-runtime / oneAPI tooling installed):
//
//   # Option A -- ocloc (Intel NEO):
//   ocloc compile -file busy_kernel.cl -spv_only -output busy_kernel \
//                 -options "-cl-std=CL2.0" -device skl
//
//   # Option B -- clang + llvm-spirv:
//   clang -cc1 -triple spir64-unknown-unknown -O2 -emit-llvm-bc \
//         -finclude-default-header busy_kernel.cl -o busy_kernel.bc
//   llvm-spirv busy_kernel.bc -o busy_kernel.spv
//
// Drop the resulting `busy_kernel.spv` next to this .cl file. CMake
// embeds it via file(READ ... HEX) at configure time.

__kernel void busy(__global uint *out, int loop_count) {
    int tid = (int)get_global_id(0);
    uint val = (uint)tid * 2654435761u + 1u;
    const uint factor = 1664525u;
    for (int i = 0; i < loop_count; ++i) {
        val = val * factor + 1013904223u;
    }
    out[tid] = val;
}
