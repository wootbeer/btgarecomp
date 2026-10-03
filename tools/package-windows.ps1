# Builds a Release copy of the game and packages it as a zip for distribution.
#
# Run from a "Developer PowerShell for VS 2022" (x64) in the repo root, after
# the usual one-time steps in BUILDING.md (N64Recomp built and run, so
# RecompiledFuncs/, RecompiledPatches/ and rsp/ exist):
#
#   .\tools\package-windows.ps1 -Version 0.1.0-beta
#
# Output: dist\BattleTanxGARecompiled-<version>-windows.zip containing the exe,
# its DLLs, the assets folder (loaded relative to the working directory, which
# is the exe's folder when launched from Explorer), README and license.
# Uses its own build folder (build-release) so a Debug dev build is untouched.
param(
    [string]$Version = "beta",
    [string]$BuildDir = "build-release",
    [string]$ClangCl = $(if ($env:BTGA_CLANGCL) { $env:BTGA_CLANGCL } else { "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe" })
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
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
