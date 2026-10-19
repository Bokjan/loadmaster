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

# shellcheck source=lib/common.sh
source "${REPO_ROOT}/scripts/lib/common.sh"

lm_prefer_ninja "$build_dir"
# -S/-B instead of `cd build && cmake ..`, so an absolute or nested
# build_dir works too. LOADMASTER_LINK_COMPILE_COMMANDS=OFF: a release
# build shouldn't repoint the developer's clangd link at its tree.
cmake -S "${REPO_ROOT}" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
      -DLOADMASTER_LINK_COMPILE_COMMANDS=OFF
# An explicit job count: see lm_jobs.
cmake --build "$build_dir" -j "$(lm_jobs)"
cp "$build_dir/src/loadmaster" "./$binary_name"
strip "$binary_name"
file "$binary_name"
