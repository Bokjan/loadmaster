#!/bin/bash
# Fail if a LOADMASTER_SILENT binary prints anything: -h / -v must exit 0
# and a bad option 1, all without a byte on stdout or stderr, and no LOG_*
# site may survive in the binary (every one embeds "(%s:%d)", the level
# tags live in util/log.cc). CI runs this on every silent release
# artifact; scripts/check_obfuscation.sh covers the obfuscated payloads.
#
# Usage:
#     scripts/check_silent.sh <binary>...

set -o errexit
set -o pipefail
set -o nounset

if [[ $# -eq 0 ]]; then
    echo "usage: $(basename "$0") <binary>..." >&2
    exit 2
fi

readonly MARKERS='\(%s:%d\)|<WARN>'

status=0
for binary in "$@"; do
    ok=1
    # "<args>:<expected exit code>"; none of these starts the load.
    for run in "-h:0" "-v:0" "--not-an-option:1" "-l:1"; do
        args="${run%:*}"
        want="${run##*:}"
        rc=0
        out="$("${binary}" ${args} 2>&1)" || rc=$?
        if [[ "${rc}" -ne "${want}" || -n "${out}" ]]; then
            echo "FAIL: ${binary} ${args}: exit ${rc} (want ${want}), ${#out} bytes of output" >&2
            ok=0
        fi
    done
    if found="$(strings "${binary}" | grep -E "${MARKERS}")"; then
        echo "FAIL: ${binary} still contains log strings:" >&2
        echo "${found}" | head -n 5 | sed 's/^/    /' >&2
        ok=0
    fi
    if [[ "${ok}" -eq 1 ]]; then
        echo "ok: ${binary}"
    else
        status=1
    fi
done
exit "${status}"
