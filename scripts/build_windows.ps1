# Build a portable Windows x86_64 release of loadmaster.
#
# Requirements (one-time):
#   * Visual Studio 2022 with the "Desktop development with C++" workload,
#     OR Build Tools for Visual Studio 2022.
#     (We need MSVC 19.30+ for std::jthread.)
#   * CMake >= 3.15 (bundled with VS 2022).
#   * (Optional, for AMD GPU support): HIP SDK for Windows, on PATH.
#
# Usage (PowerShell, from the repo root):
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

$ErrorActionPreference = 'Stop'

$RepoRoot  = Resolve-Path "$PSScriptRoot\.."
$BuildDir  = Join-Path $RepoRoot 'build_release_windows'
$DistDir   = Join-Path $RepoRoot 'dist'
$Output    = Join-Path $DistDir 'loadmaster-windows-x86_64.exe'

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error "cmake not found in PATH. Open a 'Developer PowerShell for VS 2022' or install CMake."
}

if (Test-Path $BuildDir) {
    Remove-Item -Recurse -Force $BuildDir
}
New-Item -ItemType Directory -Path $DistDir -Force | Out-Null

# Use the multi-config Visual Studio generator on x64; build Release.
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
