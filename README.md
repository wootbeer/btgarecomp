# BattleTanx: Global Assault Recompiled

A native PC port of BattleTanx: Global Assault, made by statically
recompiling the N64 game with [N64Recomp](https://github.com/N64Recomp/N64Recomp)
and running it on [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
with [RT64](https://github.com/rt64/rt64) for rendering.

Unofficial, and not affiliated with the rights holders. **No game data is
included.** You need your own dump of the game to build or play this.

This project follows the same approach as
[battle-tanx-recomp](https://github.com/bdragoncore/battle-tanx-recomp) (a
recompilation of the original BattleTanx), but targets the sequel,
**BattleTanx: Global Assault**, which is a distinct binary requiring its own
reverse-engineered symbol table and its own set of instruction/hook patches.
Nothing from the original game's symbol table carries over directly.

## Status

This project is in the early reverse-engineering stage — no symbol table or
working build yet. See [STATUS.md](STATUS.md) for the detailed, dated
research log (ROM identification, splat-based disassembly progress, the
current open question about the game's overlay system) and
[PROGRESS.md](PROGRESS.md) for the build-system side (what the eventual
N64Recomp/CMake build still needs once real symbols exist).

## Getting started

There is nothing runnable yet — see PROGRESS.md and BUILDING.md.

## Building

See [BUILDING.md](BUILDING.md).

## Credits

- [N64Recomp and N64ModernRuntime](https://github.com/N64Recomp) by Mr-Wiseguy
  and contributors, which this port is built with
- [RT64](https://github.com/rt64/rt64) by Dario and contributors, the renderer
- [RecompFrontend](https://github.com/N64Recomp/RecompFrontend) by the N64Recomp
  contributors, the launcher and input layer
- [battle-tanx-recomp](https://github.com/bdragoncore/battle-tanx-recomp) by
  bdragoncore, the recompilation of the original BattleTanx this project's
  build-system layout is modeled on
- [splat](https://github.com/ethteck/splat) and its N64 tooling
  (`spimdisasm`, `rabbitizer`), used for the ROM-splitting work in
  `tools/` and logged in STATUS.md
- [VPW64Recomp](https://github.com/jessetbh/VPW64Recomp) and
  [GGA-Recomp](https://github.com/dantheman11294/GGA-Recomp), the closest
  public precedents for reverse-engineering a previously-undocumented N64
  game from scratch for a recomp, and the main references the
  splat-based side of this project borrows its approach from

## License

The project's own code is GPL-3.0; see [COPYING](COPYING). The executable
contains the game's code, recompiled from a dump, and the game itself remains
the property of its rights holders.
