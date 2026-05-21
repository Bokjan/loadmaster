#!/bin/bash
# Build and run the loadmaster unit-test suite.
#
# Configures CMake with LOADMASTER_BUILD_TESTS=ON, builds every test
# binary, and runs them through CTest. Designed to be safe to invoke
# from CI: a non-zero exit code means at least one test failed.
#
# Usage:
#     scripts/run_tests.sh                # Debug build, full test run
#     scripts/run_tests.sh --release      # Release-with-asserts build
#     scripts/run_tests.sh --filter Foo*  # Run only tests matching pattern
#     scripts/run_tests.sh --clean        # Wipe build dir before configuring
#     scripts/run_tests.sh --help
#
# Output:
#     build_tests/   (CMake build tree, reused on subsequent runs)
#
# This script is intentionally simple -- it does NOT install anything,
# does NOT touch the release build trees used by build_linux.sh /
# build_macos.sh / build_windows.ps1, and does NOT run the loadmaster
# binary itself.

set -o errexit
set -o pipefail
set -o nounset

usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --release           Configure with CMAKE_BUILD_TYPE=Release (default: Debug)
  --filter <pattern>  Pass --tests-regex <pattern> to ctest, e.g. 'Cli.*'
  --clean             Remove the build directory before configuring
  -j <N>              Parallel build/test jobs (default: nproc)
  -h, --help          Show this message

The build tree is reused at build_tests/ across runs so incremental
edits compile fast. Use --clean to force a from-scratch configure
(needed e.g. after editing tests/CMakeLists.txt's FetchContent block).
EOF
}

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly REPO_ROOT
readonly BUILD_DIR="${REPO_ROOT}/build_tests"

BUILD_TYPE="Debug"
FILTER=""
CLEAN=0
# Default to host CPU count; fall back to 4 if nproc isn't available
# (e.g. on a stripped-down container).
JOBS="$(command -v nproc >/dev/null 2>&1 && nproc || echo 4)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --filter)
            FILTER="${2:-}"
            if [[ -z "${FILTER}" ]]; then
                echo "error: --filter requires a pattern" >&2
                exit 2
            fi
            shift 2
            ;;
        --clean)
            CLEAN=1
            shift
            ;;
        -j)
            JOBS="${2:-}"
            if [[ -z "${JOBS}" ]]; then
                echo "error: -j requires a number" >&2
                exit 2
            fi
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

if ! command -v cmake >/dev/null 2>&1; then
    echo "error: cmake is required" >&2
    exit 1
fi

if [[ "${CLEAN}" -eq 1 ]]; then
    echo "==> cleaning ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

echo "==> configuring (${BUILD_TYPE})"
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
    -DLOADMASTER_BUILD_TESTS=ON \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

echo "==> building (-j ${JOBS})"
cmake --build "${BUILD_DIR}" -j "${JOBS}"

echo "==> running tests"
CTEST_ARGS=(--output-on-failure -j "${JOBS}")
if [[ -n "${FILTER}" ]]; then
    CTEST_ARGS+=(--tests-regex "${FILTER}")
fi
ctest --test-dir "${BUILD_DIR}" "${CTEST_ARGS[@]}"
