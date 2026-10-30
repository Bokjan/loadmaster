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

# The full version (e.g. 0.9.0 or 0.9.0-rc1) that CMake parsed from
# src/core/version.h into <build_dir>/version.txt at configure time.
lm_version() {
    local file="$1/version.txt"
    if [[ ! -f "${file}" ]]; then
        echo "error: ${file} not found (configure first)" >&2
        return 1
    fi
    tr -d '[:space:]' <"${file}"
}

# Print the SHA-256 of <file> in hex.
lm_sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1"
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1"
    else
        sha256 -r "$1"   # FreeBSD base
    fi | cut -d ' ' -f 1
}
