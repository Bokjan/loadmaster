#pragma once

// Embedded SPIR-V binary for the Intel busy kernel. The actual byte array
// is generated at CMake configure time from src/gpu/intel/busy_kernel.spv
// (see src/CMakeLists.txt). When LOADMASTER_OBFUSCATE=ON the
// embedded bytes are XOR-masked with the rolling key + index pattern
// from util::obfuscate, so the SPIR-V header magic / opcode bytes do not
// appear verbatim in the binary; IntelDevice::Init() decodes them back
// into a heap buffer immediately before zeModuleCreate. If the .spv
// file is not present in the source tree, CMake instead writes an
// empty stub and sets kBusyKernelSpirvAvailable=false; the Intel
// backend then disables itself at runtime so other vendors (and the
// OpenCL backend) can still take over.

#include <cstddef>
#include <cstdint>

#include "util/obfuscate.h"

// Generated at CMake configure time into
// <build>/generated/gpu/intel/intel_spirv_data.h. The build adds
// <build>/generated to the include path, so this resolves the same way
// regardless of whether the .spv was present.
#include "gpu/intel/intel_spirv_data.h"

namespace gpu::intel {

// True iff the build system found a usable busy_kernel.spv and embedded
// it. When false, kBusyKernelSpirv may be a zero-length placeholder.
constexpr bool kBusyKernelSpirvAvailable = (kBusyKernelSpirvSize > 0);

// Per-backend XOR key. Must stay in sync with INTEL_SPIRV_KEY in
// src/CMakeLists.txt -- the build-time encoder and the runtime
// decoder need to use identical values.
inline constexpr std::uint32_t kBusyKernelSpirvKey = 0xB52E4C8Du;

}  // namespace gpu::intel
