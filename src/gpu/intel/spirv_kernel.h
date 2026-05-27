#pragma once

// Embedded SPIR-V binary for the Intel busy kernel. The actual byte array
// is generated at CMake configure time from src/gpu/intel/busy_kernel.spv
// (see src/CMakeLists.txt, target intel_spirv_kernel). If the .spv file
// is not present in the source tree, CMake instead writes an empty stub
// and sets kBusyKernelSpirvAvailable=false; the Intel backend then
// disables itself at runtime so other vendors (and the OpenCL backend)
// can still take over.
//
// Regenerating the .spv (one-shot, on any Linux box with Intel compute-
// runtime tools installed -- see busy_kernel.cl for commands).

#include <cstddef>
#include <cstdint>

// Generated at CMake configure time into
// <build>/generated/gpu/intel/intel_spirv_data.h. The build adds
// <build>/generated to the include path, so this resolves the same way
// regardless of whether the .spv was present.
#include "gpu/intel/intel_spirv_data.h"

namespace gpu::intel {

// True iff the build system found a usable busy_kernel.spv and embedded
// it. When false, kBusyKernelSpirv may be a zero-length placeholder.
constexpr bool kBusyKernelSpirvAvailable = (kBusyKernelSpirvSize > 0);

}  // namespace gpu::intel
