#!/bin/bash
# Build a portable Linux release of loadmaster for the host's
# architecture (or one passed via --arch).
#
# Uses a manylinux image so the resulting binary runs on any distro with
# at least that image's glibc, regardless of the build host's:
#   * manylinux2014 (default): CentOS 7, glibc 2.17 -- virtually every
#     distro of the last decade. CentOS 7 is end-of-life, but the image is
#     still maintained by PyPA and only its toolchain ends up mattering.
#   * manylinux_2_28: AlmaLinux 8, glibc 2.28, newer GCC -- drops RHEL 7 /
#     CentOS 7, Ubuntu < 18.10, Debian < 10.
#
# Usage:
#     scripts/build_linux.sh                 # auto-detect host arch
#     scripts/build_linux.sh --arch x86_64
#     scripts/build_linux.sh --arch aarch64
#     scripts/build_linux.sh --manylinux 2_28
#     scripts/build_linux.sh --silent        # LOADMASTER_SILENT build
#     scripts/build_linux.sh --incremental   # reuse the build tree
#     scripts/build_linux.sh --help
#
# Output:
#     dist/loadmaster-<version>-linux-<arch>
#     dist/loadmaster-<version>-linux-<arch>.sha256
#     (--silent: dist/loadmaster-<version>-silent-linux-<arch>[.sha256])
#
# Cross-architecture builds (e.g. building aarch64 on an x86_64 host)
# work transparently if qemu-user-static binfmt is registered with
# Docker, but are much slower than native. Run on a native host of the
# target arch when possible.
#
# The container runs as the invoking user, so the build tree and the
# binary stay owned by you, and `strip` runs inside the container (a
# native-only host binutils can't read a foreign-arch ELF).
#
# Reproducibility: the default image is the floating manylinux tag. The
# digest actually used is printed after each build; to pin it, set
#     MANYLINUX_IMAGE=quay.io/pypa/manylinux2014_<arch>@sha256:<digest>

set -o errexit
set -o pipefail
set -o nounset

usage() {
    cat <<EOF
Usage: $(basename "$0") [--arch <x86_64|aarch64>] [--manylinux <2014|2_28>]
                        [--silent] [--incremental] [--help]

  --arch <arch>         Target architecture. Defaults to the host's arch
                        (x86_64 or aarch64). Cross-arch needs qemu-user-static.
  --manylinux <policy>  2014 (glibc 2.17, default) or 2_28 (glibc 2.28).
  --silent              Build the variant that never prints anything
                        (LOADMASTER_SILENT=ON), named ...-silent-linux-<arch>.
  --incremental         Reuse the existing build tree instead of starting
                        from scratch (faster; for iteration, not releases).
  --help, -h            Show this message.

Output: dist/loadmaster-<version>[-silent]-linux-<arch> (+ .sha256)
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
POLICY="2014"
SILENT=0
INCREMENTAL=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch)
            ARCH="${2:-}"
            shift 2
            ;;
        --manylinux)
            POLICY="${2:-}"
            shift 2
            ;;
        --silent)
            SILENT=1
            shift
            ;;
        --incremental)
            INCREMENTAL=1
            shift
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

case "${POLICY}" in
    2014) IMAGE_PREFIX="quay.io/pypa/manylinux2014" ;;
    2_28) IMAGE_PREFIX="quay.io/pypa/manylinux_2_28" ;;
    *)
        echo "error: unsupported --manylinux '${POLICY}' (expected 2014 or 2_28)" >&2
        exit 2
        ;;
esac

case "${ARCH}" in
    x86_64 | aarch64) IMAGE="${MANYLINUX_IMAGE:-${IMAGE_PREFIX}_${ARCH}}" ;;
    "")
        echo "error: could not auto-detect host arch ($(uname -m)); pass --arch explicitly" >&2
        exit 2
        ;;
    *)
        echo "error: unsupported --arch '${ARCH}' (expected x86_64 or aarch64)" >&2
        exit 2
        ;;
esac

# The silent variant gets its own build tree and an artifact name of its own.
if [[ "${SILENT}" -eq 1 ]]; then
    SILENT_CMAKE=ON
    VARIANT="silent-"
    BUILD_SUFFIX="_silent"
else
    SILENT_CMAKE=OFF
    VARIANT=""
    BUILD_SUFFIX=""
fi

readonly ARCH POLICY IMAGE INCREMENTAL SILENT_CMAKE VARIANT BUILD_SUFFIX
readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly BUILD_DIR="${REPO_ROOT}/build_release_manylinux${POLICY}_${ARCH}${BUILD_SUFFIX}"
readonly DIST_DIR="${REPO_ROOT}/dist"

# shellcheck source=lib/common.sh
source "${REPO_ROOT}/scripts/lib/common.sh"

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

readonly BUILD_REL="${BUILD_DIR#"${REPO_ROOT}"/}"

# Older versions of this script ran the container as root and left
# root-owned build trees / dist files behind, which the unprivileged
# container below can't replace; hand them back from inside a (root)
# container.
mkdir -p "${DIST_DIR}" 2>/dev/null || true
if [[ ! -w "${DIST_DIR}" ]] || [[ -n "$(find "${DIST_DIR}" ! -user "$(id -u)" -print 2>/dev/null | head -n 1)" ]]; then
    echo "note: taking back ownership of root-owned dist/ via the container" >&2
    docker run --rm -v "${REPO_ROOT}:/work" "${IMAGE}" chown -R "$(id -u):$(id -g)" /work/dist
fi
if [[ "${INCREMENTAL}" -eq 0 ]] && ! rm -rf "${BUILD_DIR}" 2>/dev/null; then
    echo "note: removing root-owned ${BUILD_REL} via the container" >&2
    docker run --rm -v "${REPO_ROOT}:/work" "${IMAGE}" rm -rf "/work/${BUILD_REL}"
fi

# LOADMASTER_LINK_COMPILE_COMMANDS=OFF: the container sees the repo at
# /work, so a compile_commands.json symlink created there would dangle on
# the host (and clobber the developer's own link). Ninja is used when the
# image has it, for a fresh tree only (see lm_prefer_ninja). The artifact
# name needs the version, which CMake writes to version.txt at configure.
docker run --rm \
    -u "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -v "${REPO_ROOT}:/work" \
    -w /work \
    "${IMAGE}" \
    bash -c "
        set -e
        source scripts/lib/common.sh
        lm_prefer_ninja '${BUILD_REL}'
        cmake -B '${BUILD_REL}' -DCMAKE_BUILD_TYPE=Release -DLOADMASTER_LINK_COMPILE_COMMANDS=OFF \\
              -DLOADMASTER_SILENT=${SILENT_CMAKE}
        cmake --build '${BUILD_REL}' -j \"\$(lm_jobs)\"
        out=\"dist/loadmaster-\$(lm_version '${BUILD_REL}')-${VARIANT}linux-${ARCH}\"
        cp '${BUILD_REL}/src/loadmaster' \"\${out}\"
        strip \"\${out}\"
    "

OUTPUT="${DIST_DIR}/loadmaster-$(lm_version "${BUILD_DIR}")-${VARIANT}linux-${ARCH}"
readonly OUTPUT
lm_write_sha256 "${OUTPUT}"

file "${OUTPUT}"
echo
echo "Built:  ${OUTPUT}"
echo "SHA256: $(cut -d ' ' -f 1 "${OUTPUT}.sha256")"
echo "Image:  $(docker image inspect --format '{{index .RepoDigests 0}}' "${IMAGE}" 2>/dev/null || echo "${IMAGE}")"
