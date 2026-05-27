# Build and run the loadmaster unit-test suite on Windows.
#
# Configures CMake with LOADMASTER_BUILD_TESTS=ON, builds every test
# binary, and runs them through CTest. Designed to be safe to invoke
# from CI: a non-zero exit code means at least one test failed.
#
# Usage (any PowerShell, Windows 10/11):
#     scripts\run_tests.ps1                       # Debug build, full test run
#     scripts\run_tests.ps1 -Release              # Release build
#     scripts\run_tests.ps1 -Filter 'Cli.*'       # Only tests matching pattern
#     scripts\run_tests.ps1 -Clean                # Wipe build dir before configuring
#     scripts\run_tests.ps1 -Jobs 8               # Override parallelism
#     scripts\run_tests.ps1 -?                    # Help
#
# Output:
#     build_tests\   (CMake build tree, reused on subsequent runs)
#
# Mirrors scripts/run_tests.sh on POSIX. Same locator strategy as
# scripts/build_windows.ps1: prepends Visual Studio's bundled CMake
# (and CTest, shipped alongside it) to PATH for this process only,
# so the user does not need a "Developer PowerShell for VS 2022".
#
# This script is intentionally simple -- it does NOT install anything,
# does NOT touch the release build trees used by build_*.* scripts,
# and does NOT run the loadmaster binary itself.

[CmdletBinding()]
param(
    [switch]$Release,
    [string]$Filter = "",
    [switch]$Clean,
    [int]$Jobs = 0
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Resolve-Path "$PSScriptRoot\.."
$BuildDir = Join-Path $RepoRoot 'build_tests'

if ($Release) {
    $BuildType = 'Release'
} else {
    $BuildType = 'Debug'
}

if ($Jobs -le 0) {
    # Default to host CPU count; fall back to 4 if NUMBER_OF_PROCESSORS
    # is unset for some reason.
    $envCpu = [Environment]::ProcessorCount
    if ($envCpu -gt 0) { $Jobs = $envCpu } else { $Jobs = 4 }
}

# ---------------------------------------------------------------------------
# Locate Visual Studio + bundled CMake/CTest. Same trick build_windows.ps1
# uses: vswhere.exe (shipped with every VS 2017+ installer at a fixed
# location) finds the VS install root, then we prepend the bundled CMake
# to PATH for this process.
# ---------------------------------------------------------------------------
function Get-VsInstallPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        return $null
    }
    $path = & $vswhere -latest -products '*' `
                       -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                       -property installationPath 2>$null
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($path)) {
        return $null
    }
    return $path.Trim()
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $vsPath = Get-VsInstallPath
    if ($vsPath) {
        $bundledCMake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
        if (Test-Path (Join-Path $bundledCMake 'cmake.exe')) {
            Write-Host "Using CMake bundled with Visual Studio at: $bundledCMake"
            $env:PATH = "$bundledCMake;$env:PATH"
        }
    }
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error @"
cmake.exe not found.

Tried:
  1. PATH
  2. Visual Studio's bundled CMake (via vswhere.exe).

Fix one of:
  * Install Visual Studio 2022 with the 'Desktop development with C++'
    workload AND the 'C++ CMake tools for Windows' component (default).
  * OR install standalone CMake (>= 3.15) from https://cmake.org and
    ensure cmake.exe is on PATH.
"@
}

# ctest.exe lives next to cmake.exe in every supported install layout
# (both standalone and VS-bundled); requiring an additional locator
# would just produce different error wording on the same failure mode.
if (-not (Get-Command ctest -ErrorAction SilentlyContinue)) {
    Write-Error "ctest.exe not found on PATH; expected it next to cmake.exe."
}

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "==> cleaning $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

Write-Host "==> configuring ($BuildType)"
# Visual Studio 2022 is a multi-config generator on Windows; build type
# is selected at build/test time via --config rather than at configure
# time. We still pass CMAKE_BUILD_TYPE for the benefit of single-config
# generators someone might select via CMAKE_GENERATOR env var.
& cmake -S $RepoRoot -B $BuildDir `
        -G "Visual Studio 17 2022" -A x64 `
        -DLOADMASTER_BUILD_TESTS=ON `
        "-DCMAKE_BUILD_TYPE=$BuildType"
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

Write-Host "==> building (-j $Jobs, $BuildType)"
& cmake --build $BuildDir --config $BuildType --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

Write-Host "==> running tests"
$ctestArgs = @(
    '--test-dir', $BuildDir,
    '-C', $BuildType,
    '--output-on-failure',
    '-j', $Jobs
)
if (-not [string]::IsNullOrEmpty($Filter)) {
    $ctestArgs += @('--tests-regex', $Filter)
}
& ctest @ctestArgs
if ($LASTEXITCODE -ne 0) { throw "ctest reported failures" }
