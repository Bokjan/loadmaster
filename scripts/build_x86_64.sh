#!/bin/bash
# Build a portable x86_64 Linux release of loadmaster.
#
# Uses the manylinux2014 image (CentOS 7 / glibc 2.17) so the resulting
# binary runs on virtually any Linux distro published in the last decade,
# regardless of the build host's glibc version.
#
# Run on any x86_64 Linux host with Docker installed:
#     scripts/build_x86_64.sh
#
# Output:
#     dist/loadmaster-x86_64

set -o errexit
set -o pipefail
set -o nounset

readonly IMAGE="quay.io/pypa/manylinux2014_x86_64"
readonly ARCH="x86_64"
readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly BUILD_DIR="${REPO_ROOT}/build_release_${ARCH}"
readonly DIST_DIR="${REPO_ROOT}/dist"
readonly OUTPUT="${DIST_DIR}/loadmaster-${ARCH}"

if ! command -v docker >/dev/null 2>&1; then
    echo "error: docker is required" >&2
    exit 1
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
