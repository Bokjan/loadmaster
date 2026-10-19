#!/bin/bash
# Build loadmaster locally with the host's native toolchain.
#
# This is the "just configure + make" helper. It does NOT use the
# manylinux2014 Docker image (see build_linux.sh for that), so the
# resulting binary may not run on machines with an older glibc than
# the build host. Use this for quick local iteration; use
# scripts/build_linux.sh when you need a portable release artifact.
#
# Usage:
#     scripts/build_local.sh                    # default: ./loadmaster, ./rel_build
#     scripts/build_local.sh my_loadmaster      # custom output name
#     scripts/build_local.sh my_loadmaster bld  # also custom build dir

set -o errexit

if [ $# -ge 1 ] && ([ "$1" == "-h" ] || [ "$1" == "--help" ]); then
    echo "USAGE: $0 [binary_name] [build_dir]"
    exit 0
fi

binary_name="loadmaster"
build_dir="rel_build"

if [ $# -ge 1 ]; then
    binary_name=$1
fi
if [ $# -ge 2 ]; then
    build_dir=$2
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

# -S/-B instead of `cd build && cmake ..`, so an absolute or nested
# build_dir works too.
cmake -S "${REPO_ROOT}" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
# Compute an explicit jobs count and pass it to `cmake --build`. We
# cannot use `cmake --build . -j` (without a number): CMake 3.x simply
# forwards a bare `-j` to the underlying build tool, which works for
# GNU make ("unlimited parallelism") but fails for FreeBSD's base
# bmake ("option requires an argument -- j"). Using `nproc` would be
# Linux-only; instead we use `getconf` which is POSIX and present on
# Linux/macOS/FreeBSD alike, with a hard-coded "2" fallback that is
# safe (slow but correct) on the off chance _NPROCESSORS_ONLN is
# unavailable.
jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
cmake --build "$build_dir" -j "$jobs"
cp "$build_dir/src/loadmaster" "./$binary_name"
strip "$binary_name"
file "$binary_name"
