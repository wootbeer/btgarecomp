# Builds a Release copy of the game and packages it as a zip for distribution.
#
# Run from any PowerShell in the repo root, after the usual one-time steps in
# BUILDING.md (N64Recomp built and run, so RecompiledFuncs/, RecompiledPatches/
# and rsp/ exist). The script sets up Visual Studio's x64 build environment
# itself (a Developer PowerShell often defaults to x86, which can't link x64):
#
#   Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
#   .\tools\package-windows.ps1 -Version 0.1.0-beta
#
# Output: dist\BattleTanxGARecompiled-<version>-windows.zip containing the exe,
# its DLLs, the assets folder (loaded relative to the working directory, which
# is the exe's folder when launched from Explorer), README and license.
# Uses its own build folder (build-release) so a Debug dev build is untouched.
param(
    [string]$Version = "beta",
    [string]$BuildDir = "build-release",
    [string]$ClangCl = $env:BTGA_CLANGCL
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

# Visual Studio's x64 build environment (compiler libraries, Windows SDK, Ninja).
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "Visual Studio not found (no vswhere.exe) -- see BUILDING.md." }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "No Visual Studio install with the C++ tools found -- see BUILDING.md." }
if ($env:VSCMD_ARG_TGT_ARCH -ne "x64") {
    & (Join-Path $vsPath "Common7\Tools\Launch-VsDevShell.ps1") -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
if (-not $ClangCl) { $ClangCl = Join-Path $vsPath "VC\Tools\Llvm\x64\bin\clang-cl.exe" }

Set-Location $root

foreach ($required in @("RecompiledFuncs", "RecompiledPatches", "rsp")) {
    if (-not (Test-Path $required)) {
        throw "$required\ is missing -- run the N64Recomp / RSPRecomp steps in BUILDING.md first."
    }
}
if (-not (Test-Path $ClangCl)) {
    throw "clang-cl not found at $ClangCl -- pass -ClangCl <path> or set `$env:BTGA_CLANGCL."
}

cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_C_COMPILER=$ClangCl" "-DCMAKE_CXX_COMPILER=$ClangCl"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
cmake --build $BuildDir --target BattleTanxGARecompiled
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

$name = "BattleTanxGARecompiled-$Version-windows"
$stage = Join-Path $root "dist\$name"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null

foreach ($file in @("BattleTanxGARecompiled.exe", "SDL2.dll", "dxil.dll", "dxcompiler.dll")) {
    Copy-Item (Join-Path $BuildDir $file) $stage
}
Copy-Item "assets" $stage -Recurse
Copy-Item "README.md", "COPYING" $stage

$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
Write-Host "Packaged $zip"
