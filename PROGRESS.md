# Progress / Roadmap

Where the port stands. STATUS.md has the full history of how each piece was
found and fixed.

The goal is the original game, running natively: fixes are limited to what
the PC port needs (timing, rendering and runtime differences from the N64),
and leave the game's own behaviour, look and balance alone.

## Working

- **The game**: intro, menus, campaign missions (played through mission 13
  so far) and the credits, on Windows.
- **Recompilation**: all game code is in the first MB of the ROM (the rest
  is assets), with no overlays. The symbol file
  (`BattleTanxGASyms/battletanxga.us.rev0.syms.toml`) has been corrected by
  hand wherever a merged or mis-sized function turned up.
- **Graphics** through RT64: higher resolutions, MSAA, higher framerates
  (interpolated) and widescreen (Expand) with the gameplay HUD anchored to
  the screen edges and letterboxed cutscenes. Includes a fix in RT64 itself
  for distant flicker in Expand (`lib-patches/rt64/`).
- **Audio**: the game's RSP audio microcode, recompiled from the ROM
  (`n_aspMain.us.rev0.toml`), with the volume setting and a latency cap.
- **Saves**: Controller Pak emulation, stored as standard `.mpk` files.
- **Controllers**: keyboard and gamepads through the frontend, rumble, and
  local multiplayer (General tab).
- **Timing**: the game times motion with the clock; its frame time is
  snapped to whole VIs, and the few places that truncated the per-frame
  step to an integer (which hardware's slower ~20 fps hid) are fixed
  proportionally (`src/game/frame_dt_fix.cpp`).
- **Crash reports**: `crash_log.txt` gives the exception, the module, any
  C++ error message and the game function that faulted
  (`src/main/crash_handler.cpp`).
- **Windows release** packaging (`tools/package-windows.ps1`), with the
  third-party license notices and no build-machine paths in the exe.

## Known issues

- Tank shadows can show on the near side of sand mounds when the tank is
  behind them. Probably the original game: the shadow is drawn well below
  the hidden tank, on the near slope (so the game places it there), and an
  emulator with a different decal method shows the same. Left as is; only
  real hardware or an accurate emulator (ares, ParaLLEl-RDP) would confirm.
- The Edge's stun on enemy tanks may last too long; not reproduced yet.
- Quitting can fault in a game thread after the runtime frees memory. This
  is caught and exits silently, but a cleaner shutdown would be better.

## Next

- Bug reports from beta testers.
- Linux: an experimental build is published (`tools/package-linux.sh`);
  waiting on reports from SteamOS and other Linux players.
- A SteamOS tester's crash under Proton; the next build's crash log
  should say what failed.
- macOS is not set up.

## Where things are

| Path | What |
|---|---|
| `battletanxga.us.rev0.toml` | N64Recomp config: hooks, instruction patches, function fixes |
| `BattleTanxGASyms/` | Symbol file (function boundaries) |
| `n_aspMain.us.rev0.toml` | RSP audio microcode config |
| `src/main/` | Frontend: window, audio, input, config, crash reporting |
| `src/game/` | Game-specific fixes called from hooks |
| `patches/` | Whole-function replacements compiled for MIPS |
| `assets/` | UI font, icons (`tools/make_icons.py`) and their licenses |
| `licenses/` | License notices for shipped files not in a submodule |
| `lib-patches/` | Patches applied to submodules at configure time |
| `tools/` | Analysis scripts and release packaging |
| `syms/` | Raw symbol lists from the original analysis |
