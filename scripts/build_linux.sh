#!/bin/bash
# Build a portable Linux release of loadmaster for the host's
# architecture (or one passed via --arch).
#
# Uses the manylinux2014 image (CentOS 7 / glibc 2.17) so the resulting
# binary runs on virtually any Linux distro published in the last
# decade, regardless of the build host's glibc version.
#
# Usage:
#     scripts/build_linux.sh                 # auto-detect host arch
#     scripts/build_linux.sh --arch x86_64
#     scripts/build_linux.sh --arch aarch64
#     scripts/build_linux.sh --help
#
# Output:
#     dist/loadmaster-<arch>
#
# Cross-architecture builds (e.g. building aarch64 on an x86_64 host)
# work transparently if qemu-user-static binfmt is registered with
# Docker, but are much slower than native. Run on a native host of the
# target arch when possible.

set -o errexit
set -o pipefail
set -o nounset

usage() {
    cat <<EOF
Usage: $(basename "$0") [--arch <x86_64|aarch64>] [--help]

  --arch <arch>   Target architecture. Defaults to the host's arch
                  (x86_64 or aarch64). Cross-arch needs qemu-user-static.
  --help, -h      Show this message.

Output: dist/loadmaster-<arch>
EOF
}

# Auto-detect default architecture.
detect_arch() {
    case "$(uname -m)" in
        x86_64 | amd64) echo "x86_64" ;;
        aarch64 | arm64) echo "aarch64" ;;
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
    x86_64)  IMAGE="quay.io/pypa/manylinux2014_x86_64"  ;;
    aarch64) IMAGE="quay.io/pypa/manylinux2014_aarch64" ;;
    "")
        echo "error: could not auto-detect host arch ($(uname -m)); pass --arch explicitly" >&2
        exit 2
        ;;
    *)
        echo "error: unsupported --arch '${ARCH}' (expected x86_64 or aarch64)" >&2
        exit 2
        ;;
esac

readonly ARCH IMAGE
readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly BUILD_DIR="${REPO_ROOT}/build_release_${ARCH}"
readonly DIST_DIR="${REPO_ROOT}/dist"
readonly OUTPUT="${DIST_DIR}/loadmaster-${ARCH}"

if ! command -v docker >/dev/null 2>&1; then
    echo "error: docker is required" >&2
    exit 1
fi

# Friendly note when building for a non-host architecture.
HOST_ARCH="$(uname -m)"
if [[ "${HOST_ARCH}" == "amd64" ]]; then HOST_ARCH="x86_64"; fi
if [[ "${HOST_ARCH}" == "arm64" ]]; then HOST_ARCH="aarch64"; fi
if [[ "${HOST_ARCH}" != "${ARCH}" ]]; then
    echo "note: building for ${ARCH} on ${HOST_ARCH} host; relying on" >&2
    echo "      qemu-user-static binfmt for cross-emulation (much slower" >&2
    echo "      than native). Run on a native ${ARCH} host when possible." >&2
fi

mkdir -p "${DIST_DIR}"
rm -rf "${BUILD_DIR}"

docker run --rm \
    -v "${REPO_ROOT}:/work" \
    -w /work \
    "${IMAGE}" \
    bash -c "
        set -e
        cmake -B '${BUILD_DIR##${REPO_ROOT}/}' -DCMAKE_BUILD_TYPE=Release
        cmake --build '${BUILD_DIR##${REPO_ROOT}/}' -j
    "

cp "${BUILD_DIR}/src/loadmaster" "${OUTPUT}"
strip "${OUTPUT}"
file "${OUTPUT}"
echo
echo "Built: ${OUTPUT}"
