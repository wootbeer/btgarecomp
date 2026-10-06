#!/usr/bin/env bash
# Builds a Release copy of the game and packages it as a .tar.gz for Linux
# (experimental: the Linux build is not tested on real hardware yet).
#
# Run from the repo root after the one-time steps in BUILDING.md (N64Recomp
# run, so RecompiledFuncs/ exists, with the ROM at the repo root):
#
#   tools/package-linux.sh 0.1.1
#
# Output: dist/BattleTanxGARecompiled-<version>-linux-x86_64.tar.gz with the
# binary, a launcher script that runs it from its own folder (assets/ is
# loaded relative to the working directory), assets, README, license and the
# third-party license notices. The system provides SDL2, GTK 3, FreeType and
# Vulkan. Uses its own build folder (build-release-linux, or $BUILD_DIR).
set -euo pipefail

version="${1:-dev}"
root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${BUILD_DIR:-$root/build-release-linux}"
cd "$root"

if [ ! -d RecompiledFuncs ]; then
    echo "RecompiledFuncs/ is missing -- run the N64Recomp step in BUILDING.md first." >&2
    exit 1
fi

cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --target BattleTanxGARecompiled

name="BattleTanxGARecompiled-$version-linux-x86_64"
stage="dist/$name"
rm -rf "$stage"
mkdir -p "$stage/licenses"

install -m 755 "$build_dir/BattleTanxGARecompiled" "$stage/"
strip "$stage/BattleTanxGARecompiled"
cat > "$stage/BattleTanxGARecompiled.sh" <<'EOF'
#!/bin/sh
# Runs the game from this folder, where its assets are.
cd "$(dirname "$(readlink -f "$0")")" && exec ./BattleTanxGARecompiled "$@"
EOF
chmod 755 "$stage/BattleTanxGARecompiled.sh"
cp -r assets "$stage/"
cp README.md COPYING "$stage/"

# License notices for the third-party code built into the binary.
licenses=(
    "N64ModernRuntime:lib/N64ModernRuntime/COPYING"
    "N64Recomp:lib/N64ModernRuntime/N64Recomp/LICENSE"
    "RT64:lib/rt64/LICENSE"
    "plume:lib/rt64/src/contrib/plume/LICENSE"
    "hlslpp:lib/rt64/src/contrib/hlslpp/LICENSE"
    "imgui:lib/rt64/src/contrib/imgui/LICENSE.txt"
    "implot:lib/rt64/src/contrib/implot/LICENSE"
    "im3d:lib/rt64/src/contrib/im3d/LICENSE"
    "ddspp:lib/rt64/src/contrib/ddspp/LICENSE"
    "nativefiledialog-extended:lib/rt64/src/contrib/nativefiledialog-extended/LICENSE"
    "stb:lib/rt64/src/contrib/stb/LICENSE"
    "xxHash:lib/rt64/src/contrib/xxHash/LICENSE"
    "zstd:lib/rt64/src/contrib/zstd/LICENSE"
    "re-spirv:lib/rt64/src/contrib/re-spirv/LICENSE"
    "SPIRV-Cross:lib/rt64/src/contrib/spirv-cross/LICENSE"
    "miniz:lib/N64ModernRuntime/thirdparty/miniz/LICENSE"
    "o1heap:lib/N64ModernRuntime/thirdparty/o1heap/LICENSE"
    "RmlUi:lib/RecompFrontend/recompui/lib/RmlUi/LICENSE.txt"
    "lunasvg:lib/RecompFrontend/recompui/lib/lunasvg/LICENSE"
    "plutovg:lib/RecompFrontend/recompui/lib/lunasvg/plutovg/LICENSE"
    "GamepadMotionHelpers:lib/RecompFrontend/lib/GamepadMotionHelpers/LICENSE"
    "SlotMap:lib/SlotMap/README.md"
)
for entry in "${licenses[@]}"; do
    file="${entry#*:}"
    if [ -f "$file" ]; then
        cp "$file" "$stage/licenses/${entry%%:*}.txt"
    else
        echo "warning: license file not found: $file" >&2
    fi
done

tar -C dist -czf "dist/$name.tar.gz" "$name"
echo "Packaged dist/$name.tar.gz"
