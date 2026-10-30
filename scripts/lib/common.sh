# Helpers shared by the POSIX build scripts. Sourced, not executed:
#     source "${REPO_ROOT}/scripts/lib/common.sh"
# Sticks to bash 3.2 (macOS's /bin/bash) features.

# Number of online CPUs, for `cmake --build -j <N>`. getconf is POSIX
# (Linux / macOS / FreeBSD), unlike `nproc`; a bare `-j` is no option
# either, since CMake forwards it to the build tool and FreeBSD's bmake
# rejects it.
lm_jobs() {
    getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2
}

# Use Ninja for a fresh build tree when it is installed and the caller
# didn't pick a generator (CMAKE_GENERATOR, honoured by CMake >= 3.15).
# An existing tree keeps its generator: CMake refuses to switch it.
lm_prefer_ninja() {
    local build_dir="$1"
    if [[ -z "${CMAKE_GENERATOR:-}" && ! -f "${build_dir}/CMakeCache.txt" ]] &&
        command -v ninja >/dev/null 2>&1; then
        export CMAKE_GENERATOR=Ninja
    fi
}

# The full version (e.g. 0.8.0 or 0.8.0-rc1) that CMake parsed from
# src/core/version.h into <build_dir>/version.txt at configure time.
lm_version() {
    local file="$1/version.txt"
    if [[ ! -f "${file}" ]]; then
        echo "error: ${file} not found (configure first)" >&2
        return 1
    fi
    tr -d '[:space:]' <"${file}"
}

# Write <file>.sha256 next to <file>, in the format `sha256sum -c` /
# `shasum -a 256 -c` read: "<hex>  <basename>".
lm_write_sha256() {
    local dir base
    dir="$(dirname "$1")"
    base="$(basename "$1")"
    (
        cd "${dir}"
        if command -v sha256sum >/dev/null 2>&1; then
            sha256sum "${base}"
        elif command -v shasum >/dev/null 2>&1; then
            shasum -a 256 "${base}"
        else
            # FreeBSD base: `sha256 -r` prints "<hex> <file>".
            sha256 -r "${base}" | awk '{ print $1 "  " $2 }'
        fi
    ) >"$1.sha256"
}
