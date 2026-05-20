#!/bin/bash
# Build a portable aarch64 (arm64) Linux release of loadmaster.
#
# Uses the manylinux2014 image (CentOS 7 / glibc 2.17) so the resulting
# binary runs on virtually any aarch64 Linux distro published in the last
# decade.
#
# Run on an aarch64 Linux host with Docker installed:
#     scripts/build_aarch64.sh
#
# Output:
#     dist/loadmaster-aarch64

set -o errexit
set -o pipefail
set -o nounset

readonly IMAGE="quay.io/pypa/manylinux2014_aarch64"
readonly ARCH="aarch64"
readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly BUILD_DIR="${REPO_ROOT}/build_release_${ARCH}"
readonly DIST_DIR="${REPO_ROOT}/dist"
readonly OUTPUT="${DIST_DIR}/loadmaster-${ARCH}"

if ! command -v docker >/dev/null 2>&1; then
    echo "error: docker is required" >&2
    exit 1
fi

# Sanity check: complain (not fail) if the host arch obviously doesn't
# match. People with binfmt + qemu-user-static can still cross-emulate,
# so we don't hard-fail here.
host_arch="$(uname -m)"
if [[ "${host_arch}" != "aarch64" && "${host_arch}" != "arm64" ]]; then
    echo "warning: host arch is '${host_arch}', not aarch64 -- relying on" >&2
    echo "         qemu-user-static binfmt for cross-emulation; this works" >&2
    echo "         but is much slower than building on a native arm64 host." >&2
fi

mkdir -p "${DIST_DIR}"
rm -rf "${BUILD_DIR}"

docker run --rm \
    -v "${REPO_ROOT}:/work" \
    -w /work \
    "${IMAGE}" \
    bash -c "
        set -e
        cmake3 -B '${BUILD_DIR##${REPO_ROOT}/}' -DCMAKE_BUILD_TYPE=Release
        cmake3 --build '${BUILD_DIR##${REPO_ROOT}/}' -j
    "

cp "${BUILD_DIR}/src/loadmaster" "${OUTPUT}"
strip "${OUTPUT}"
file "${OUTPUT}"
echo
echo "Built: ${OUTPUT}"
