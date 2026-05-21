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

mkdir -p "$build_dir" && cd "$build_dir"
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j
cd ..
cp "$build_dir/src/loadmaster" "./$binary_name"
strip "$binary_name"
file "$binary_name"
