#!/bin/bash
# Build a release of loadmaster for macOS (Apple Silicon or Intel).
#
# Requirements (one-time):
#   * Xcode Command Line Tools: `xcode-select --install` (provides
#     AppleClang, ld64, libc++, and the Mach / libproc headers).
#   * CMake >= 3.15 (Homebrew: `brew install cmake`, or use the CMake
#     bundled with a full Xcode install).
#
# Usage:
#     scripts/build_macos.sh                 # auto-detect host arch
#     scripts/build_macos.sh --arch arm64
#     scripts/build_macos.sh --arch x86_64
#     scripts/build_macos.sh --help
#
# Output:
#     dist/loadmaster-macos-<arch>
#
# Notes:
#   * Unlike the Linux build, we do NOT statically link libc++/libSystem:
#     Apple does not ship static archives for them and links against the
#     OS-bundled dylibs are the supported (and only) path. The resulting
#     binary is forward-compatible across macOS versions >= the
#     deployment target chosen below.
#   * The GPU module degrades gracefully: macOS has neither the NVIDIA
#     CUDA driver nor the AMD HIP runtime, so DlopenAny() will fail to
#     find any candidate library and the GPU resource manager simply
#     disables itself. CPU and memory modules work normally.

set -o errexit
set -o pipefail
set -o nounset

usage() {
    cat <<EOF
Usage: $(basename "$0") [--arch <arm64|x86_64>] [--help]

  --arch <arch>   Target architecture. Defaults to the host's arch.
                  Cross-compiling is supported on Apple Silicon hosts via
                  CMAKE_OSX_ARCHITECTURES (no Rosetta needed at build
                  time, but the resulting x86_64 binary will run under
                  Rosetta 2 on Apple Silicon).
  --help, -h      Show this message.

Output: dist/loadmaster-macos-<arch>
EOF
}

detect_arch() {
    case "$(uname -m)" in
        arm64 | aarch64) echo "arm64" ;;
        x86_64 | amd64)  echo "x86_64" ;;
        *) echo "" ;;
    esac
}

ARCH="$(detect_arch)"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch)
            ARCH="${2:-}"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

case "${ARCH}" in
    arm64|x86_64) ;;
    "")
        echo "error: could not auto-detect host arch ($(uname -m)); pass --arch explicitly" >&2
        exit 2
        ;;
    *)
        echo "error: unsupported --arch '${ARCH}' (expected arm64 or x86_64)" >&2
        exit 2
        ;;
esac

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "error: this script must be run on macOS (Darwin); got $(uname -s)" >&2
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "error: cmake not found on PATH. Install via 'brew install cmake'." >&2
    exit 1
fi

readonly ARCH
readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly BUILD_DIR="${REPO_ROOT}/build_release_macos_${ARCH}"
readonly DIST_DIR="${REPO_ROOT}/dist"
readonly OUTPUT="${DIST_DIR}/loadmaster-macos-${ARCH}"

# Deployment target: 11.0 covers every Apple-Silicon-capable macOS plus
# the last few Intel releases. Bump if you need APIs from a newer SDK.
readonly DEPLOYMENT_TARGET="11.0"

mkdir -p "${DIST_DIR}"
rm -rf "${BUILD_DIR}"

cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="${ARCH}" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}"
cmake --build "${BUILD_DIR}" -j

cp "${BUILD_DIR}/src/loadmaster" "${OUTPUT}"
strip -x "${OUTPUT}"   # -x: keep external symbols (required by dlopen)
file "${OUTPUT}"
echo
echo "Built: ${OUTPUT}"
