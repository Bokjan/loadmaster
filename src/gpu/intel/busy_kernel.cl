// loadmaster busy kernel for the Intel (Level Zero) backend.
//
// Compiled offline into SPIR-V, then embedded into the binary at build
// time (see src/gpu/intel/spirv_kernel.h). Mirrors the NVIDIA PTX and
// AMD HIP busy kernels: each work-item runs an LCG-style 32-bit
// integer multiply-add loop and stores the final value back to a
// scratch buffer so the optimizer cannot eliminate the loop, and every
// `mem_stride` iterations reads a word of the `-gm` block and writes it
// back changed (see gpu::MemSweep; mem_words = 0 turns that off). Keep it
// identical to the OpenCL backend's source in gpu/opencl/cl_kernel.h.
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
// embeds it via file(READ ... HEX) at configure time (and re-configures
// automatically when the file changes).
//
// With clang + llvm-spirv on PATH, `cmake --build <dir> --target
// regen_spirv` runs Option B and overwrites the committed .spv.
//
// Provenance of the committed busy_kernel.spv: SPIR-V 1.4, generator
// 0x0006000e = Khronos LLVM/SPIR-V Translator, translator version 14 --
// Option B, with Debian clang 19.1.7 and llvm-spirv-19 19.1.6 (LLVM
// 19.1.7). When regenerating, note the versions here.

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
