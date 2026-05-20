# Build a portable Windows x86_64 release of loadmaster.
#
# Requirements (one-time):
#   * Visual Studio 2022 with the "Desktop development with C++" workload,
#     OR Build Tools for Visual Studio 2022.
#     (We need MSVC 19.30+ for std::jthread.)
#   * The "C++ CMake tools for Windows" individual component (selected by
#     default with the C++ workload). This bundles a CMake >= 3.15.
#   * (Optional, for AMD GPU support): HIP SDK for Windows, on PATH.
#
# Usage (any PowerShell, Windows 10/11):
#     scripts\build_windows.ps1
#
# Output:
#     dist\loadmaster-windows-x86_64.exe
#
# The resulting executable links the C/C++ runtime statically (/MT) so it
# does not need the Visual C++ Redistributable on the target machine.
# It still dynamically loads NVIDIA / AMD GPU drivers via LoadLibrary at
# runtime, so a fresh Windows machine with an NVIDIA driver "just works"
# for the GPU module; AMD requires the HIP SDK runtime.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$RepoRoot  = Resolve-Path "$PSScriptRoot\.."
$BuildDir  = Join-Path $RepoRoot 'build_release_windows'
$DistDir   = Join-Path $RepoRoot 'dist'
$Output    = Join-Path $DistDir 'loadmaster-windows-x86_64.exe'

# ---------------------------------------------------------------------------
# Locate Visual Studio + bundled CMake without requiring the user to start
# from a "Developer PowerShell for VS 2022". We use vswhere.exe (shipped
# with every VS 2017+ installer at a fixed location) to find the VS install
# root, then prepend the bundled CMake to PATH for this process only.
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

if (Test-Path $BuildDir) {
    Remove-Item -Recurse -Force $BuildDir
}
New-Item -ItemType Directory -Path $DistDir -Force | Out-Null

# Use the multi-config Visual Studio generator on x64; build Release. The
# generator itself locates the VS toolchain via the registry, so we don't
# need vcvars to be loaded -- only cmake.exe needs to be reachable.
& cmake -S $RepoRoot -B $BuildDir -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

& cmake --build $BuildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

$Built = Join-Path $BuildDir 'src\Release\loadmaster.exe'
if (-not (Test-Path $Built)) {
    throw "expected build output not found: $Built"
}
Copy-Item -Force $Built $Output

Write-Host ""
Write-Host "Built: $Output"
Get-Item $Output | Select-Object Name, Length, LastWriteTime | Format-List
