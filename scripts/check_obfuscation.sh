#!/bin/bash
# Fail if a loadmaster binary contains the plaintext that
# LOADMASTER_OBFUSCATE is supposed to hide: the CLI help text and the GPU
# kernel sources (CUDA PTX, HIP, OpenCL, Metal). Optimisers have defeated
# the XOR masking before (LLVM constant-folded it back on macOS), so CI
# runs this on every release artifact.
#
# Usage:
#     scripts/check_obfuscation.sh <binary>...

set -o errexit
set -o pipefail
set -o nounset

if [[ $# -eq 0 ]]; then
    echo "usage: $(basename "$0") <binary>..." >&2
    exit 2
fi

# One marker per payload: help text, PTX entry point, HIP / OpenCL /
# Metal kernel signatures.
readonly MARKERS='target CPU usage|\.entry busy|void busy'

status=0
for binary in "$@"; do
    if found="$(strings "${binary}" | grep -E "${MARKERS}")"; then
        echo "FAIL: ${binary} contains plaintext that should be obfuscated:" >&2
        echo "${found}" | sed 's/^/    /' >&2
        status=1
    else
        echo "ok: ${binary}"
    fi
done
exit "${status}"
