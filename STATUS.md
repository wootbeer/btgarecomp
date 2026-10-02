# Status

Last updated: 2026-10-01, in a Claude Code cloud session (a different sandbox
from the one that wrote the entries below).

## 2026-10-02, round 105: letterbox sides via the frame clear, independent of HUD Ratio

Round 104 results:
- The HUD is right at all three HUD Ratio settings, and menus are fine.
- With HUD Ratio **Expand** (and 16:9 on a 16:9 window), the cutscene
  sides are now black.
- At **Original** they still show sky. RT64 positions anchored origins by
  the HUD Ratio percentage, so at Original anchored bars (and their
  widened scissor) stay in 4:3 by design.

The letterbox shouldn't depend on a HUD setting, so it's now fixed at the
source.
- `func_8007A250` builds each frame's colour clear: `G_SETFILLCOLOR`
  (sky RGB from `0x801144F4`), then `G_FILLRECT` `0xF64FC3BC`, with its
  display-list head in the stack variable `0x24($fp)`.
- A hook right after that fill rect (`0x8007A5A4`) checks whether the
  previous frame was letterboxed. If so, it appends:
  - a black fill of the whole screen, which RT64 stretches to the window
  - the sky colour again over just the 3D view rectangle, which isn't
    full width, so RT64 maps it into the 4:3 area unchanged
- The view rectangle is derived from the bars `func_800D56FC`'s box
  drawer drew: left bar's right edge, right bar's left edge, top bar's
  bottom, bottom area's top. The box hooks now only track the bars; they
  no longer anchor them.

## 2026-10-02, round 104: widen the scissor for anchored draws; right-origin offset

Round 103 results:
- No crash.
- Pickup messages are good.
- With HUD Ratio at 16:9 or Expand, the map and ammo box are cut off or
  missing. At Original they're fine.
- The cutscene side areas are still sky-coloured. `[BTGA BAR]` showed
  `func_800D56FC`'s box path drawing the bars (x 286..320, players=1,
  scale 1.333), so the anchoring was being emitted.

Two RT64 behaviours (`rt64_framebuffer_renderer.cpp`, `rt64_rdp.cpp`)
explain both:
1. Every rect is clipped to the call's scissor. The scissor is converted
   with its own origins, so the game's 0..320 scissor (no origin) maps to
   the centred 4:3 area, and anything anchored past it is clipped. That
   cut off the bars' extensions and the HUD at the wider HUD ratios.
   While anything is anchored, `emit()` now sends `gEXPushScissor` +
   `gEXSetScissor(G_SC_NON_INTERLACE, LEFT, RIGHT, 0, 0, 0, 240)` (the
   whole window), and `gEXPopScissor` on reset. No HUD or box routine
   sets its own scissor, so a single push/pop around each element is
   safe.
2. `RDP::movedFromOrigin` adds the framebuffer width to right-anchored
   coordinates, so they must be given relative to the right edge.
   Right-anchored HUD elements and the right bar's right edge now get a
   -320*4 offset (as Zelda64Recomp's HUD does with -SCREEN_WIDTH*4).
   Before, the right-anchored ammo box was placed a screen width to the
   right.

Diagnostic removed.

## 2026-10-02, round 103: fix round 102's crash at level start; cutscene bar diagnostic

Round 102 results:
1. Title screen, logos and menus are fixed.
2. The cutscene side areas are still sky-coloured.
3. The game crashed right after picking a tank at level start.

**Crash.** `func_800C7C10` (the map) copies the caller's display-list
head to a stack local (`sp+0x18`), draws through it (`func_800C7650`
gets the same local), and writes it back at `0x800C81F8`. Its stack frame
is then popped before `jr $ra`. Round 102 recorded the pointer the last
texture rect wrote through (the local) and used it for the reset at the
map's exit hook, after the frame was gone, which corrupted the caller's
stack. Emission now records a separate *home* pointer for the reset: the
map's entry `a0`, or `0x803A5944` for interpreter elements and the
widgets and number drawers they call.

**Cutscene bars.** Neither the interpreter fill hook (round 101) nor the
`func_800D56FC` hook (round 102) changed them. The only other fill-rect
builder, the routine at `0x800BBE24` (merged into `func_800BBDC0`'s
symbol), has no references and looks dead. Temporary `[BTGA BAR]`
diagnostic: every 2 s each fill path prints its call count, last x
range, the player-count byte (`anchoring_active` requires <= 2) and the
widescreen scale.

## 2026-10-02, round 102: HUD anchoring limited to the gameplay HUD; cutscene bars via func_800D56FC

Round 101 test results:
1. Cutscene side areas still sky-coloured.
2. HUD mostly right, but should sit even closer to the edges; pickup
   messages partly cut off.
3. Parts of the main menu cut off.
4. One ground piece still pops in at level start.

New: the 3DO logo, the title screen and other static images were cut in
half. They're sprite strips drawn by the same interpreter, and round
101's per-sprite x rule anchored the left strips left and pushed the
right ones off-screen.

The script data explains the structure. The 1-player HUD script at
`0x8011DEF0` has 16-byte elements (opcode, colour, x, y, ...,
data/function pointer). It holds the health bar (op 16 at 129,216),
sprite frames (op 3/10 at 262), numbers (op 8), and op 23 widgets: the
kill counter `0x800C9A3C` at x 264, and `0x800C8484` at x 0. The map
(`func_800C7C10`) isn't an interpreter element at all; it's drawn
through an object table (`0x8011DDA0`, `func_800C8350`).

Changes (`src/game/widescreen.cpp`):
- At interpreter entry, a script counts as gameplay HUD only if it
  contains the kill-counter widget. Only HUD scripts are anchored, so
  title screens, logos and menus are left alone.
- Each HUD element is anchored by its own script x, at dispatch
  (`0x800BCC04`, element in `$fp`), and only for element types seen in
  the HUD (ops 3, 8, 10, 14, 16, 23). Text is never anchored, so strings
  aren't split.
- The map is anchored left for the whole of `func_800C7C10`.
- Anchored HUD elements move a further 16 px outward (rect offsets
  of ±64 in 10.2), since the original layout keeps a TV-safe margin.
- Round 101's interpreter fill hook didn't catch the cutscene bars, so
  they must come from `func_800D56FC`'s box drawer (FA + F6 written
  through its own display-list head `0x803A69E4`). The edge-bar rule is
  applied there too (hooks at `0x800D5B10` / `0x800D5BF0`).
- Resets now go back to the display-list head the alignment was emitted
  on.

## 2026-10-02, round 101: widescreen HUD anchoring and cutscene letterbox bars

The round 99b/100 logs settled both questions.

**Cutscene sides.** The intro frame draws:
- a sky-coloured fill-mode clear of 0..319 x 0..239 (RT64 snaps it to the
  scissor and stretches it, since it spans the full width)
- black 1-cycle bars: 0..34 x 0..134, 286..320 x 0..134, 34..286 x 0..19
- a black 0..320 x 134..240 area, which is also stretched

The side bars aren't full width, so they stay inside the 4:3 frame, and
the stretched sky shows beside them.

**HUD.** The call sites map onto the screenshot:
- `func_800C7C10`: the map (frame through `func_800C7650`, image at
  `0x800C7CA0`, marker at `0x800C7FFC`)
- `func_800C9A3C`: the kill counter
- interpreter sprite call `0x800BD3A8`: health frame, ammo box and weapon
  icon
- `0x800BD524`: the health fill
- numbers through `func_8009700C`, text through `func_80096F48`

All of it is drawn by `func_800BC9F4`, a 2D overlay script interpreter (a
jump table of 23 element types at `0x800731A0`), which also draws the
cutscene bars. Every element writes to the display-list head at
`0x803A5944`.

Fix (`src/game/widescreen.cpp` plus hooks in `func_800BC9F4` and
`func_8007C364`). In Expand, with 1-2 players, each element gets an RT64
`gEXEnable` + `gEXSetRectAlign` written at the display-list head, then a
reset after it:
- map widget: left; kill counter widget: right
- sprite and number elements: by x (left of 120 → left, from 200 →
  right, else centred), so the health bar stays centred and the counter
  under the map follows it. Text strings are never anchored, so centred
  cutscene text stays whole.
- fill rects: a bar touching the left or right edge gets only that edge
  anchored (letterbox bars reach the window edge); other HUD panels use
  the x rule

How far an anchored element moves follows the Graphics tab's HUD Ratio
(default Clamp 16:9). The round 99/100 diagnostics are removed.

## 2026-10-02, round 100: cutscene side areas in Expand (temporary diagnostic)

User screenshots:
- **Gameplay HUD in Expand** (all of it sits in the centred 4:3 area).
  Positions in game pixels:
  - map: x 31-101, y 134-207
  - "0" counter below it: x 89-94, y 211-222
  - weapon/ammo box: x 261-285, y 192-220
  - kill counter and icon: x 256-282, y 20-34
  - health bar: x 130-192, y 214-222

  Wanted:
  - anchored **left**: the map, and the "0" with it
  - anchored **right**: the ammo box and the kill counter
  - **centred**: the health bar
- **Letterboxed intro cutscene in Expand.** The areas left and right of
  the 4:3 image show the scene's sky/clear colour for game y ~0-133. The
  bottom (text area) is black across the whole window, and the 4:3
  borders around the 3D view are black. RT64 stretches a fill rect to
  the window edges when it spans the framebuffer pair's scissor width
  (`rt64_framebuffer_renderer.cpp`), so some full-width rect is being
  stretched, but the static DL doesn't show which.

Temporary diagnostic: `btga_dl_fill_diag` (in `src/game/hud_diag.cpp`,
called from `btga_fix_screen_edge_scissors` every VI, throttled to 2 s)
walks both gfx task display lists. It prints every `G_FILLRECT` in draw
order with its rect, cycle type, fill/prim colour and active scissor, as
`[BTGA FILL]`.

## 2026-10-02, round 99: HUD anchoring groundwork (temporary diagnostic)

Goal: in Expand, anchor the map and ammo HUD elements to the screen
corners and leave the health bar centred. RT64 does this with extended
GBI: `gEXEnable` (an F3DEX2 `G_SPNOOP` with RT64's magic number), then
`gEXSetRectAlign(left/right origin)` around the element's texture
rectangles. The Graphics tab's HUD Ratio setting then scales how far
toward the edges they move.

Findings so far:
- Every texture rectangle is built by `func_8007C364` (the only code
  that builds `G_TEXRECT`). It's called directly from `func_80096A54` and
  through the wrappers `func_8007C9B8` (26 callers), `func_8007CE64` and
  `func_8007D39C`.
- The frame display-list write pointer is the global `0x803A5944`, and
  the matrix allocator is `0x803A5930` (`func_800BC6EC`). Native code can
  append RT64 commands at `*0x803A5944`.
- `func_800BC6EC` (fovy 37, 4:3, own look-at) and the `func_800C7650`
  9-slice box drawer look like menu code, not the in-game HUD.
- Every HUD sprite position is computed at runtime, so a temporary
  diagnostic (`src/game/hud_diag.cpp`, entry hooks on the four functions
  above) prints each call site's sprite position range every 3 seconds
  as `[BTGA HUD] site=...`, to match call sites to the map and ammo.

Round 99b: the first log reported every site as `00000000`. Recompiled
code turns `jal` into a direct C call and never writes `$ra`, so an entry
hook can't see its caller. Each of the 36 `jal`s to these functions now
gets a hook that records its own address first, and the log also counts
distinct sprite pointers per site.

## 2026-10-02, round 98: widescreen (Expand) culling

**Round 97 confirmed:** the right/bottom strip is gone. With it fixed,
RT64's Expand mode now widens the 1-player 3D view on its own (user
confirmed: wider, not stretched). RT64 (`rt64_projection_processor.cpp`,
`G_EX_ASPECT_AUTO`) does this for any projection whose viewport and
scissor cover the full framebuffer-scissor width. The old 319-pixel
scissor had failed that test, which is why only the full-width clear
used to extend.

New report: in Expand, ground textures (and effects) near the new side
edges pop in and out. The game culls against its own 4:3 cone.
`func_800AC9E8` builds a per-player cull record each frame
(`0x802194B4`, stride 0x28): camera position, normalized ground-plane
direction, and that direction times a per-layout factor k from
camera+0x168. `func_800A72C0` sets k from `0x80072C8C...`: 2.29 for 1P,
1.12 for 2P halves, 2.2 for quadrants. These are about 1/tan of the half
horizontal FOV for fovy 37 at aspect 4:3 / 8:3. Consumers (e.g.
`func_800ACCB4`) test `forward > 0 && |k * lateral| < forward`.

Fix (`src/game/widescreen.cpp`): hooks right after both `lwc1 $f0,
0x168($s0)` loads in `func_800AC9E8` (`0x800ACBF0`, `0x800ACC04`) divide
k by the widen factor. That factor mirrors RT64's Expand target: window
aspect / (4/3), at least 1, and 1 in Original. `update_gfx` in
`main.cpp` computes it every frame. Only cameras whose viewport display
list spans the full width are widened (`0x01000138/150/168`); RT64
leaves quadrant views at 4:3.

## 2026-10-02, round 97: screen-edge scissor fix retargeted at the live display-list copy

**Round 95/96 confirmed:** Controller Pak saving works on the user's
machine. The right/bottom strip was still there.

Round 94 patched the viewport display lists where they sit in the loaded
image (`0x80127F68...`), but the game never draws from there. Nothing
references those addresses directly; the frame calls them through
segment 1 (`0x01000138`, `0x01000150`, ...). Segment 1's base comes from
`0x801144F0`, set at init (`0x80079F70`-`0x80079FD0`): the game DMAs the
0x400-byte block at ROM `0xB7E30` (the image's `0x80127E30`-`0x80128230`)
into a buffer at `0x803B17B0` and stores that address there. Round 94's
writes landed in the unused original.

`btga_fix_screen_edge_scissors` now reads the buffer address from
`0x801144F0` and patches the same six `G_SETSCISSOR` words at the same
offsets in the copy, still only while each holds its ROM value.

## 2026-10-02, round 96: fix round 95's crash on launch

Round 95's build closed immediately on launch (never reached Task
Manager). Reproduced in the sandbox: SIGSEGV in
`Config::get_option_value` from `btga::config::get_local_multiplayer()`
in `main()`. `game_config.cpp` kept a pointer to the `Config&` that
`create_general_tab()` returned, but recompui keeps the tabs in a
`std::vector` (`ui_config.cpp`, `configs.push_back`), and creating the
Graphics/Controls/Sound/Mods tabs afterwards reallocated it. The options
are now read through `recompui::config::get_general_config()` on every
call. After the fix the binary starts and idles at the launcher under Xvfb.

## 2026-10-02, round 95: cleanup of diagnostics; Controller Pak saves, rumble, multiplayer, audio fixes

**Cleanup (deferred since round 65).** Removed every temporary
diagnostic, keeping the fixes they led to:
- `src/game/vi_dispatch_diag.cpp` (the `[BTGA DEBUG v3]`, `[BTGA PACING]`
  and `[BTGA DL]` prints) deleted, along with its calls in
  `func_800A1858`'s RECOMP_PATCH and its two `syms.ld` entries.
- The round-52 `[BTGA DEBUG v2]` hook on `func_800988E8` removed from
  `battletanxga.us.rev0.toml`.
- `[BTGA DEBUG]` logging removed from `scheduler_workaround.cpp` and
  `[BTGA DT]` from `frame_dt_fix.cpp`; the yield and the dt snapping stay.
- `main.cpp`'s `checkpoint:` traces, "main() started" and "SDL Video
  Driver" prints removed; real errors and the terminate handler stay.

**Feature gaps found in an audit**, all now implemented:

1. **Saves.** The game has no cartridge save; it saves to a Controller
   Pak through libultra's osPfs* API, which stock librecomp
   (`librecomp/src/pak.cpp`) answers with "no pak". New
   `src/game/controller_pak_hle.cpp` defines all ten osPfs*_recomp
   functions (so the linker no longer pulls in `pak.cpp`) over a raw 32 KB
   `.mpk` image per controller: `saves/<game id>_pak1.mpk` (pak2..4 for the
   other ports). It uses the standard layout (ID area, mirrored inode
   table with checksum, 16-entry note table, 123 data pages), so files are
   interchangeable with Project64-style single-pak `.mpk` saves. The game
   allocates one 256-byte file per save, lists saves with osPfsFileState
   over all 16 slots, and treats error 5 as an empty slot. That flow was
   checked offline (allocate/exist/write/reload/read/state/free/delete),
   and the written image passes the ID and inode checksum rules.
2. **Rumble and pak choice.** On the N64 a controller holds one pak. The
   game probes each port with osPfsInitPak and calls osMotorInit only when
   that returns `PFS_ERR_ID_FATAL` (10) (`func_800985A0`,
   `func_80098CC8`). New General-tab options (`src/main/game_config.cpp`):
   *Player 1 Accessory* (default Controller Pak) and *Players 2-4
   Accessory* (default Rumble Pak). A Rumble Pak port answers 10 from the
   emulated pak, and the runtime's own osMotor* drives SDL rumble.
3. **Local multiplayer.** Ports 2-4 were hard-wired as unplugged. New
   *Local Multiplayer* option (applies on restart). It puts recompinput in
   multi-player mode and reports one connected port per assigned player
   (Controls → Assign players). In single-player mode only port 1 is
   connected, since every controller drives player 1 there.
4. **Audio.**
   - Stereo channels were reversed. Each stereo frame is one word-swapped
     RDRAM word, so the halves read back right-then-left (Zelda64Recomp
     swaps them the same way).
   - The Sound tab's volume was never applied.
   - The crackle came from underruns. The game sizes audio tasks from
     osAiGetLength, the runtime reserves only about 0.25 VI of headroom,
     and SDL drained in 1024-frame chunks. Now 2 VIs of headroom are
     reported, with a 512-frame device buffer.
   - Output is float. SDL resamples, because the device opens with no
     allowed changes; before, `SDL_AUDIO_ALLOW_FREQUENCY_CHANGE` could
     silently play the game's rate at the device's rate.

Builds in-sandbox; not yet run.

## 2026-10-02, round 94: right/bottom strip found -- the game's own off-by-one screen-edge scissor

The user then spotted the strip along the **bottom** edge too, a few
screen pixels thick. Round 93's `[BTGA DL]` log settled it: besides the
full-screen clear's `0..320 x 0..240`, every frame draws with scissor
**`0..319 x 0..239`**. Scissor lower-right corners are exclusive, so the
last column and row are never drawn. This is the classic
`SCREEN_WD-1, SCREEN_HT-1` off-by-one, hidden by TV overscan on hardware.
It explains everything: right *and* bottom, 1 framebuffer pixel (scales
with the window), clear colour in gameplay, stale in menus, and absent
from Bomberman Hero Recompiled on the same machine.

The scissors live in the game's static viewport display lists at
`0x80127F68...` (one per 1-3 player layout; the 4-player ones at
`0x8011CA00...` have deliberate margins). These are RDRAM data, so
`btga_fix_screen_edge_scissors` (`src/game/screen_edge_scissor_fix.cpp`,
called at the top of `func_800A1858`'s RECOMP_PATCH, `syms.ld`
`0x8F0000F4`) rewrites the six `G_SETSCISSOR` w1 words whose right or
bottom edge touches the screen border (319 -> 320, 239 -> 240). Each word
is written only while it still holds the original value (checked against
the ROM). Split-screen inner seams (x 159, y 119) are left as designed.

Builds in-sandbox; not yet run. The `[BTGA DL]` line should now show
`0..320 x 0..240` instead of `0..319 x 0..239`.

## 2026-10-02, round 93: audio works; investigating a stale strip at the right edge of the image

**Round 92 confirmed:** music and sound play and sound mostly right
(occasional slight crackle, expected with the plain `SDL_QueueAudio` push
in `main.cpp`). A crash after maxing every graphics option took about a
second to appear, consistent with GPU memory exhaustion from
resolution x downsampling x MSAA on an 8 GB card. Not treated as a bug.

New report: a thin strip at the right edge of the 4:3 image. In gameplay
it's the sky/clear colour; in menus it keeps "whatever colour it last
was". It's present at every resolution, scales with the window (a fixed
fraction of the width), and in Expand mode the whole side area shows the
same thing. Bomberman Hero Recompiled (same RT64) on the same machine
does not have it.

Ruled out so far:
- VI mode is libultra's standard NTSC LAF1 (`osViModeTable[3]`): width
  320, `xScale 0x200`, hStart/hEnd 108/748. RT64's `fbSize()` gives
  exactly 320x240.
- Game framebuffer is 320 wide (`gDPSetColorImage` width-1 `0x13F` at
  `0x8007A3DC`/`0x8007A494`). The full-screen scissor (`0xED000000
  005003C0`) and clear (`0xF64FC3BC`) cover 0..320 / 0..319 inclusive.
- Every static viewport (`0x80127E30` table for 1-4 player layouts) is
  full width.
- RT64 Original aspect mode doesn't widen (`aspectRatioScale` = 1.0);
  Expand intentionally extends full-screen fill rects, which is why the
  sides fill with clear colour there.
- The syms entry named `osViExtendVStart` (`0x800FBD88`) is a
  mislabeled one-line music-player setter, recompiled as normal code. It
  has nothing to do with the VI.

Since the clear reaches the strip in gameplay but the 3D scene and menu
backgrounds don't, something in the actual frames stops short. Added
`btga_debug_dl_extents` (`src/game/vi_dispatch_diag.cpp`, run from the
swap diagnostic every 2 s). It walks both gfx tasks' F3DEX2 display lists
(OSTask `data_ptr` at +0x30, following `G_DL`, segments via
`G_MOVEWORD`) and prints `[BTGA DL]` with the distinct color images,
scissors and viewports and the widest fill/texture rectangle. Builds;
not yet run.

## 2026-10-01, round 92: audio tasks run; the music sequencer's command handlers split at libmus's command table

Round 91's run got further: `[sp] Audio task: 801EC768` now alternates
with gfx tasks, so `M_AUDTASK`s are submitted and run through the
recompiled `n_aspMain`. Then `Failed to find function at 0x800FE1C4`,
inside the entry named `Ftron`. Round 83 predicted this: with audio live,
the libmus music sequencer runs, and its command handlers are reached
through a pointer table. The syms had several handlers merged per
named entry.

Found the libmus command table at `0x80126590`: 45 pointers, matching
libmus's 45 sequence commands. Every pointer that already landed on a
named entry agreed with libmus's command order (slot 0 `Fstop`, 3
`Fportoff`, 10 `Fviboff`, 18 `Fenvon`, 20 `Ftron`, 24 `Fwobbleoff`, 26
`Fveloff`, 29 `Fstereo`, 32 `Fprint`, 42 `Fchangefx`; slot 16
`Fenvelope` is round 84's recovered `func_800FAE70`). Split the 10
merged entries at the 32 table pointers that weren't function starts,
naming each piece by its libmus command. Every split point follows a
`jr $ra` + delay slot with no crossing branch. The two divide-guard
hooks keyed to `Fportoff` (`0x800fddb0`, `0x800fdf00`) moved to `Fdefa`
and `Ftempo`.

Verified in-sandbox: regenerated (1578 = 1546 + 32 functions, no
errors), hook/patch landings and switches identical, full build links.
Not yet run.

## 2026-10-01, round 91: the audio pipeline runs -- one microcode dispatch target was wrongly excluded

Round 90's first run aborted at game start with `Unhandled jump target
0x02B0 in microcode n_aspMain` (librecomp `rsp.cpp:57`, exit reason 3).
Command `0x0E` (`r26 = 0x0E011800`) dispatches through table slot 14,
`0x02B0`, which round 90 had dropped as "outside the text". But the RSP PC
is 12 bits, so it's IMEM `0x12B0`. Banjo's table holds the same value,
and RSPRecomp normalizes targets with `| 0x1000`. The code there is
`sh $t9, 0x4a($t8); j 0x10EC`, i.e. command `0x0E` enters the tail of the
handler at `0x12A8`. Added `0x12B0` to `n_aspMain.us.rev0.toml`.

The same log shows the pipeline round 90 enabled is alive: `[BTGA DEBUG
v3]` read `gate_0x801147E8=0x801ec768`, so the audio thread
(`func_80097844`) is posting real `M_AUDTASK` OSTasks and the
microcode was executing commands until it hit this one.

Verified in-sandbox: the build regenerates `rsp/n_aspMain.cpp` with
`case 0x12B0` and links. Not yet run.

## 2026-10-01, round 90: audio -- RSP audio microcode recompiled, audio library un-stubbed and its fragmented symbols merged

**Round 89 confirmed:** game speed is back to normal, motion is much
smoother, and the tank's rear logo decal no longer flickers. Moving on to
audio, which has been completely stubbed since rounds 57-58.

**RSP side.** Modeled on BanjoRecomp, which recompiles its `n_aspMain`
with N64Recomp's RSPRecomp. Ours came from the game's own audio-task
setup: `func_800FF698` passes ucode text `0x800FA210` and data
`0x801262E0`, and `0x800FA210 + 0xC60` ends exactly at round 84's
`func_800FAE70`. It's the same size as Banjo's `n_aspMain` but a
different build: our dispatch table (first 16 halfwords of the data)
holds Banjo's entries minus 4, with two empty slots and one `0x02B0`
(outside the text, an unused command). All 13 real targets follow a
`j 0x10EC` / `jr $ra`. New `n_aspMain.us.rev0.toml` (ROM offset
`0x8A210`, IMEM `0x04001080`). CMake now runs RSPRecomp to generate
`rsp/n_aspMain.cpp` (gitignored, ROM-derived, only when the ROM is
present, defining `BTGA_HAS_RSP_AUDIO`), and `get_rsp_microcode` returns
`n_aspMain` for `M_AUDTASK`. Graphics tasks stay on RT64's HLE.

**CPU side.** The n_ audio library (`0x800FFB30`-`0x80101C70`) was split
into fragments (`n_env_text_*`, `n_load_text_*`, `n_reverb_text_*`, ...)
that fall through or branch into each other. N64Recomp doesn't follow
fall-through, so each cut function returned early. That's the real
cause of the round 57/58 crashes in `n_alEnvmixerPull` / `_n_saveBuffer`
that the native stubs papered over. Merged them back into 17 whole
functions (35 fragments absorbed): adjacent pieces join while one falls
through into the next or any branch lands inside another piece; jumps to
another function's *start* stay tail calls. Every merged function ends
at its real `jr $ra`. No absorbed piece is called directly. The one
ROM-referenced piece, `0x801004F4`, is a case target in
`n_alEnvmixerPull`'s own jump table at `0x80077720`, which now resolves
(`jr $v0` at `0x80100120`, 9 targets, all inside the function).
`ignored` is now empty and `func_801000B0` is out of `stubs` (both
were fragments). Deleted `src/game/n_alEnvmixerPull_stub.cpp` and
`src/game/func_801025C0_stub.cpp`.

With `func_801025C0` real, `func_800FF698` should now build audio
command lists and call the table's `func_80097844`, which posts the
`M_AUDTASK` OSTask to `0x801147E8`; `func_800A140C` then submits it.
Round 66's spin fix in `func_80097844` becomes relevant again.

Verified in-sandbox: RSPRecomp generates `rsp/n_aspMain.cpp` from the
ROM; N64Recomp regenerates 1546 functions (1581 - 35) with no errors or
branch warnings and no remaining inter-fragment tail calls; hook/patch
landings identical; full build links. Not yet run -- expect the usual
crash-fix cycle now that audio code actually executes.

## 2026-10-01, round 89: correcting round 88 -- it fed the game double the delta time

Round 88's `[BTGA DT]` log showed both of its assumptions were wrong:

- The game's raw per-frame value is a steady **~0.75** (0.72-0.77), not
  1.0. That matches hardware: the game divides CPU-count ticks (46.875
  MHz) by the CPU clock rate (62.5 MHz), so a 30 fps frame is 0.75 and one
  VI is **0.375** units. The host-clock wobble is only about +/-3%.
- `btga_vi_tick` counted **3** per 30 fps frame (~90/s): `func_800A1858`
  runs off another message as well as VI. So round 88 substituted **1.5**
  where the game expects 0.75, making time-based motion run about 2x.
  The user's "helped in some places" can't be trusted.

Fixed: removed `btga_vi_tick` (patch call and `syms.ld` entry).
`btga_frame_dt` now snaps the game's own measured value to whole VIs
(`round(raw / 0.375) * 0.375`, steady state exactly 0.75), leaving
sub-VI values (first frame / zero) as measured. The `[BTGA DT]` line now
prints raw min/avg/max next to the value actually used.

Since the raw wobble is only +/-3%, delta time is probably *not* the main
cause of the visible stutter or the flicker (the tank's rear logo decal
flickering on its own, distant buildings flickering anywhere on screen).
Those look renderer-side and are the next thing to investigate.

Verified in-sandbox: regenerated (1581 functions, no errors), no
remaining `btga_vi_tick` references, full build links. Not yet run.

## 2026-10-01, round 88: frame-locked delta time -- the game times motion with the host clock

Round 87's pacing log ruled out presentation: steady state is exactly 30
fps (2 VIs and ~34 ms per swap, all 3 framebuffers in order, no repeats or
A->B->A). The only irregularities are occasional 100-270 ms hitches. None
of RT64's options (resolution, aspect ratio, MSAA) changed the remaining
stutter or the distant-building flicker, and the flicker can be anywhere
on screen. Thread priorities rule out the yield workarounds letting
lower-priority threads interleave: the main game thread
(`func_8009EEA0`) is priority 10, the lowest of the game's threads; the
VI dispatcher (`func_800A1290`) is 30.

Found it in the per-frame update `func_800BF80C`: it calls `osGetTime()`,
subtracts the previous frame's timestamp (`0x803A5938`), converts to us,
scales by `30.0 / 1000000.0` (constants at `0x80073290`/`0x80073294`), and
stores the result as a float at `0x803A5948`, the game's delta time in
1/30 s units (code like `func_800C68AC`/`func_800CFA84` multiplies by
it). On hardware the CPU reaches that point at the same phase of every
video frame, so it's a steady 1.0. Here `osGetTime()` is the host clock
and the main thread wakes at a slightly different moment each frame, so it
wobbles while presentation stays even, and objects step unevenly.

**Fix:** `btga_vi_tick` (called at the top of `func_800A1858`'s
RECOMP_PATCH, once per real VI, registered at `0x8F0000F4`) counts VIs.
A `[[patches.hook]]` on `func_800BF80C` right before `swc1 $f0,
0x5948($at)` (`0x800BFAC8`) calls `btga_frame_dt`
(`src/game/frame_dt_fix.cpp`). It replaces `$f0` with
VIs-since-last-frame * 0.5, falling back to the raw value on the first
frame or after a stall of more than 8 VIs (loads). A once-per-second
`[BTGA DT]` line reports the raw value's min/avg/max next to
VIs-per-frame, to confirm the wobble.

Verified in-sandbox: regenerated (1581 functions, no errors), hook lands
right before the store, `btga_vi_tick` lands at the top of the patch,
full build links. Not yet confirmed against a real run.

Separately reported: the logo decal on the back of the player's tank
flickers on its own. A coplanar decal flickering by itself is the
classic sign of depth fighting in the renderer's decal handling, a
separate issue from timing. Next, if it survives this round.

## 2026-10-01, round 87: menus confirmed; investigating remaining stutter with a frame-pacing diagnostic

**Round 86 confirmed:** the launcher and config menus now render text and
icons. Switching Graphics -> Framerate to `Original` reduced the
model stutter/flicker a lot (so RT64 interpolation was part of it), but
some remains, mostly in the credits. Distant buildings also sometimes
flicker or pop in and out.

Re-verified `patches/recompui_patches.c`'s `func_800A1858` against the
original disassembly: it matches instruction for instruction (threshold
check on `0x1F0`/`0x1EC`, `func_8007AF84(1)` = the `0xB4` "queued for
display" slot, one-shot unblank, swap, clear `0x1F0`). So frame
*selection* isn't the bug.

Added `btga_debug_swap_pacing` (`src/game/vi_dispatch_diag.cpp`, called
from the patch right before `osViSwapBuffer`, registered in
`patches/syms.ld` at `0x8F0000F0`). Once per second it prints `[BTGA
PACING]`: swaps, swap interval min/avg/max in ms, VIs per swap min/max,
repeats, A->B->A "back_and_forth" swaps, and distinct framebuffers seen.
That separates uneven swap timing (likely a side effect of the
cooperative-scheduler yield workarounds) from out-of-order presentation.
Builds in-sandbox; not yet run.

## 2026-10-01, round 86: launcher/config UI text and icons -- font family name mismatch, plus assets modeled on BanjoRecomp

**Round 85 confirmed:** Credits works. The user reports two remaining
issues. First, models stutter back and forth and sprites/textures
flicker, worst in the attract demo and credits and slight in gameplay.
Second, the recompui launcher and config menus show no text and broken
formatting (the user has been clicking blind).

Menu text: recompui's generated base stylesheet
(`lib/RecompFrontend/recompui/src/data/base_rcss.cpp`) sets `body {
font-family: "<registered family>" }`, and RmlUi names a loaded face by
the family name stored inside the font file. `main.cpp` registered
`LatoLatin-Regular.ttf` as `"Lato"`, but the file's internal family is
`LatoLatin` (read from its `name` table). So the stylesheet named a family
that was never loaded, and no text was drawn. Missing icons: recompui
loads 11 SVGs from `assets/icons/` (Caret, Cont, Keyboard, PlusKeyboard,
Question, Quit, RecordBorder, RecordSpinner, Reset, Trash, X), and none
existed here, leaving blank buttons and broken-looking layout.

Fix, modeled on BanjoRecomp (same RecompFrontend, same GPL-3 license as
this project): copied its `InterVariable.ttf` (OFL 1.1, notice in new
`assets/INTER_LICENSE.txt`), its 14 top-level `assets/icons/*.svg`, and
its `assets/promptfont/` (controller-glyph font with its own LICENSE).
`main.cpp` now registers `("InterVariable.ttf", "Inter Variable")`,
exactly as Banjo does. Banjo's own `recomp.rcss` is also a one-rule
placeholder (recompui generates the real styles in C++), so ours stays
as is. Assets resolve as `./assets/...` relative to the working
directory, so run the exe from the repo root as usual.

Builds in-sandbox; this sandbox has no GPU, so it can't render the UI.
Not yet confirmed against a real run.

On the stutter/flicker: the project's Framerate option defaults to
`Display`, i.e. RT64 interpolates up to the monitor's refresh rate, which
needs per-game matrix tagging this port doesn't have. That's the leading
suspect, and it needs the now-readable config menu to test
(Esc -> Graphics -> Framerate -> Original).

## 2026-10-01, round 85: three levels played; credits crash from a function entry starting one word early

**Round 84 confirmed on Windows:** the game played through to level 3
with no crashes. Back at the main menu, selecting Credits aborted with
`Failed to find function at 0x800F7650`. That's 4 bytes into
`func_800F764C`. Round 22 started that entry at `0x800F764C`, but that
word is a padding `nop` (after a data word at `0x800F7648`). The real
prologue (`addiu $sp, $sp, -0x30`) is at `0x800F7650`, and only that
address is referenced in the ROM. So the indirect call there found no
function start. Moved the entry to `func_800F7650` (`0x220`), leaving
the `nop` uncovered.

A scan for entries starting on padding found no other entry where the
start is unreferenced but the following prologue is. The other hits
are referenced at their own start, or are data in the `0x8011xxxx`
range.

Verified in-sandbox: regenerated (1581 functions, no errors), hook/patch
landings and switches identical, no references to the old name,
`func_800F7650` registered, full build links. Not yet confirmed against
a real run.

## 2026-10-01, round 84: mid-level crash in code the original syms never covered -- functions placed right after inline string data

Still in level 1, near where round 82's crash was, shooting / using
controls: `Failed to find function at 0x800DC214`. No syms entry covers
that address at all. It's inside a `0xCC0`-byte gap
(`0x800DB6E8`-`0x800DC3A8`) that the original symbol file left
uncovered, apparently because it starts with 8 bytes of data (`04000000
0000072e`). After that come four real functions, with clean jr-$ra
boundaries, no crossing or outgoing branches, and no `jal` callers (they
are reached only through pointers): `func_800DB6F0`, `func_800DB7A4`,
`func_800DB850` (its `jr $v0` at `0x800DBF04` resolves to a normal
3-case switch), and `func_800DC214`.

A scan of every uncovered gap in `.resident_first_mb` for `jr $ra`
found six more with the same pattern. Inline string or data comes first
(`TATE_PLAYING`, `ELLR`, `_FLAME_GANGL`, a few data words), then real
code the generator missed. Added, each validated (fully decodes, ends in
`jr $ra` + delay slot, no clean internal split, no branch leaving it
except to a known function start): `func_800866B0`, `func_800A7290`,
`func_800AA5D0`, `func_800DC7B0`, `func_800DC804`, `func_800F4D80`,
`func_800F4FF0`. The largest gap (`0x800F8DA4`-`0x800FAFBC`) is RSP
microcode and stays uncovered, except for the one CPU function at its
end, `func_800FAE70`, a pointer-referenced music-player handler. All
leading string/data bytes stay uncovered.

Verified in-sandbox: regenerated (1581 functions, no errors), hook/patch
landings identical, the only switch change is the new `0x800DBF04`
table, all 12 new functions registered, full build links. Not yet
confirmed against a real run.

## 2026-10-01, round 83: a full level played; level-complete crash in a mislabeled "osBbCardChange" entry

**Round 82 confirmed on Windows:** shooting works and a full level
plays through to completion. Right after the level was beaten, the game
aborted with `Failed to find function at 0x800CFA74`, inside the syms
entry named `osBbCardChange`. That's an iQue Player-only libultra name
that can't exist in this US ROM. The code is a run of small game
getters/setters (e.g. `0x800CFA74` is `lbu $v0, 0x65f8(0x803A0000); jr
$ra`), so the name is a mislabel like round 80's `__udiv_w_sdiv`.
Renamed to `func_800CFA50` (nothing else used the name). The batch tool
then split only this entry, into six pieces.

Verified in-sandbox: regenerated (1569 functions, no errors), hook/patch
landings and all switches identical, no references to the old name,
`func_800CFA74` registered, full build links. Not yet confirmed against
a real run.

Not done (yet): the batch tool still skips the 199 named entries that
N64Recomp recompiles normally. Some of these are known to be merged too,
e.g. the libmus music-command handlers (`Fstop`, `Fportoff`, `Fprint`,
...), which are dispatched through a pointer table. A full level played
without touching any of them, so with audio stubbed the sequencer
evidently isn't running them. Revisit once audio is implemented.

## 2026-10-01, round 82: in-game! Shooting crashed in an entry the batch tool couldn't decode

**Round 81 confirmed on Windows:** the options menu works and the game
reaches actual gameplay, playable for a few seconds. Firing aborted with
`Failed to find function at 0x800EE288`, inside `func_800EDF14`. It's a
clean boundary (real `jr $ra` at `0x800EE280`, prologue at `0x800EE288`,
no crossing branch), so why had round 78's batch missed it?
`tools/batch_split_merged_funcs.py` silently skipped any entry capstone
couldn't fully disassemble, and capstone ran in MIPS32 mode. String data
stored after a function's last return (here `66616b65`, "fake...") often
decodes as a 64-bit MIPS III op, which MIPS32 mode rejects. 15 `func_`
entries were affected.

Tool changes: disassemble in MIPS64 mode (the N64's R4300 is MIPS III),
and require every new piece to contain its own `jr $ra`, so a
ROM-referenced string after a return can never be split off as a
"function". Re-running added 5 functions across 4 entries and touched
nothing else: `func_800DD0D8` (+`func_800DD1C0`), `func_800EDF14`
(+`func_800EE288`), `func_800F7C30` (+`func_800F7D24`,
`func_800F7E64`), `func_800F8264` (+`func_800F85D8`). Of the other 11,
two are in `stubs` and the rest have no referenced clean boundary.

Verified in-sandbox: regenerated (1564 functions, no errors), hook/patch
landings and all switches identical, all 5 new functions registered,
full build links. Not yet confirmed against a real run.

## 2026-10-01, round 81: round 80 confirmed (full intro -> main menu); options-menu crash in the entry round 80 renamed

**Round 80 confirmed on Windows:** the attract demo plays all the way
through, and the game reaches the main menu. Changing options there
aborted with `Failed to find function at 0x800C0A64`, inside
`func_800C0704`. That's the entry round 80 moved and renamed from
`__udiv_w_sdiv`; round 78's batch had skipped it because it had a
library name. Re-running `tools/batch_split_merged_funcs.py` changed
only this entry. It splits nine ways, and both of its jump tables stay in
their own pieces (`jr 0x800C0774` -> `func_800C0704`, `jr 0x800C0914` ->
`func_800C08E0`). The crash address is a two-instruction "return 1"
function (`jr $ra; addiu $v0, $zero, 1`), most likely a menu callback.

Verified in-sandbox: regenerated (1559 functions, no errors), hook/patch
landings and all switches identical, `func_800C0A64` registered, full
build links. Not yet confirmed against a real run.

## 2026-10-01, round 80: demo crash was a function cut off mid-epilogue -- round 22's splits were chasing branches decoded from string data

Round 79 confirmed: the attract demo now plays until a nuke goes off and
its animation runs, then crashes with an access violation (no `Failed to
find function`). Debugger: `func_800DE52C` at `lhu $a2, 0x18($s2)`
(`0x800DE66C`), right after returning from `func_800DA4F0`. The same load
from the same `$s2` had succeeded just before that call, so the callee
was corrupting a callee-saved register.

`func_800DA4F0`'s syms entry (`0x140`) ended at `0x800DA630`, mid-way
through its own epilogue. The rest (`lw $s7..$s0`, `addiu $sp, 0x78`,
`jr $ra`) was a separate entry, `func_800DA630`. **N64Recomp never emits
a fall-through call into the next entry**, so a function cut short just
returns early. Here that meant it never restored `$s0`-`$s7` and never
popped its frame, corrupting every caller.

Round 22 made that cut on purpose, along with one at `0x800DB258`,
because `func_800C48F0` appeared to branch to both addresses. It
doesn't: the 8 bytes at `0x800C6918` are `10 54 52 4F 57 4D 4F 44`
(string data, "TROWMOD"), which disassemble as a `beq`/`bnel` pair. A
whole-table scan for entries that end without `jr`/`j` turned up one
more case of the same mistake and one off-by-8 boundary:

- `func_800C68AC` (round 78's tail piece of `func_800C48F0`): now ends
  at `0x800C6918`, leaving the 8 string bytes uncovered.
- `func_800DA4F0`: restored to its full `0x16c`; `func_800DA630` removed.
- `func_800DB1B0` + `func_800DB258` + `func_800DB4C8` (the last was the
  original syms' entry for that function's epilogue): merged back into
  one `0x338` function ending at its real `jr $ra`.
- `func_80086034`: ends at `0x8008615C`. The word there is "REMA"
  (`52 45 4D 41`), decoded as a fake `beql` into `0x80099664`.
- `func_80099534` + `func_80099664` + `func_80099690`: merged back into
  one `0x190` function. Same three-way cut, same cause.
- `func_800C0608` (`+8`) / `__udiv_w_sdiv`: the latter started 8 bytes
  early, on `func_800C0608`'s own shared `jr $ra; move $v0, $zero` exit.
  All seven `j`s to `0x800C06FC` come from inside `func_800C0608`, and
  its fall-through path returned without zeroing `$v0`. The real next
  function starts at `0x800C0704` (referenced twice as a ROM data word),
  renamed `func_800C0704`; nothing about it resembles libgcc's
  `__udiv_w_sdiv`.

The rest of that scan's game-code hits are benign: the boot entry, the
runtime-replaced exception handler, and trailing padding or data before a
real prologue. The audio-library (`n_*`) fall-throughs are left for when
audio is implemented.

Verified in-sandbox with the submodule's N64Recomp: 1551 functions
(1556 - 5 merged away), no errors; every fixed function ends in its real
`jr $ra` with no branch leaving its range; no references remain to the
removed names; hook/patch landings and all switches identical to before;
full build links. Not yet confirmed against a real run.

## 2026-10-01, round 79: round 78's batch confirmed -- the attract demo plays; one skipped entry fixed

**Round 78 confirmed on Windows:** idling at the title now plays the
intro/attract demo movie, new territory for this port. It crashed after
a while with `Failed to find function at 0x800ED804`, inside
`func_800ED4F4`, one of the two entries round 78 skipped as having an
"unresolved jump table".

Why it was skipped: the range ends with a stray `jr $zero` at
`0x800ED98C`, after the last real `jr $ra`. It's dead code that N64Recomp
emits as `LOOKUP_FUNC(0)`, not a jump table. The range's two real tables
(`jr $v0` at `0x800ED6C0` and `0x800ED87C`) were both resolved.
`tools/batch_split_merged_funcs.py` now ignores `jr $zero` when looking
for unresolved tables. Re-running it changed only this entry (round 78's
splits came out identical, and `func_80082A90` has no referenced
boundaries): `0x148` + `0x1c8` + `0x18c`. Each jump table stays in its
own piece, and the divide-guard hook at `0x800ed648` was re-pointed to
`func_800ED63C`.

Verified in-sandbox: regenerated (1556 functions, no errors), hook/patch
landings and all switches identical to before, both new pieces
registered, full build links. Not yet confirmed against a real run.

## 2026-10-01, round 78: batch split of every ROM-referenced merged-function boundary (one revertible commit)

Rounds 70-77 fixed eight `Failed to find function at 0x...` crashes one
at a time, each a merged-function boundary. Round 77's evidence (the
ROM-reference filter flagged all 8; any split with no crossing branch and
no jump-table target past it is semantically safe) justified doing the
rest in one pass. `tools/batch_split_merged_funcs.py` does it:

- Only unnamed `func_XXXXXXXX` entries, excluding the toml's `stubs` and
  `ignored` lists. Named library code (libmus `F*` handlers,
  `__udiv_w_sdiv`, `osBbCardChange`, ...) is left alone.
- Only boundaries right after a `jr $ra` + delay slot, with no
  branch/jump crossing them, whose start address is referenced in the
  ROM (literal word or `lui`/`addiu|ori` constant).
- Every resolved jump table (read from the generated `switch` statements)
  keeps its `jr` and all case targets in one piece. Entries containing an
  unresolved register jump are skipped entirely: `func_80082A90`,
  `func_800ED4F4`.
- All-zero (padding) pieces are not split off.
- Every `[[patches.hook]]`/`[[patches.instruction]]` whose address lands
  in a new piece is re-pointed to it. That was 15 hooks, all divide guards
  or yields, e.g. `func_800D6E40`'s six now live in `func_800D6EEC` and
  `func_800D7638`.

Result: 78 entries split into 203 new functions (1351 -> 1554). Verified
in-sandbox with the submodule's N64Recomp: no errors; the landing of all
180 hooks/instruction patches (by vram) is byte-for-byte identical to
before; all 186 jump tables are still emitted unchanged; and a full
build links, with the extra generated `funcs_N.c` files picked up via
round 74's `CONFIGURE_DEPENDS`. Not yet confirmed against a real run.
**If anything that worked before round 78 regresses, `git revert` this
one commit first.**

## 2026-10-01, round 77: three-way split of func_800F7EC0, and evidence on batching

Round 76 confirmed. Next idle-at-title crash: `Failed to find function
at 0x800F7F6C`, inside `func_800F7EC0`'s declared `0x1dc` range. Two
clean internal boundaries, no crossing branch, no jump table, no hooks.
`0x800F7F6C` is a 6-instruction leaf with no stack frame
(`lbu $v0, 0x21($a0)` ... `sw $v0, ($a1)`, `jr $ra`). Split `0xac` +
`0x18` + `0x118`. Verified in-sandbox (1351 functions, no errors, both
new pieces registered, full build links).

Evidence on pre-emptive batching: every merged-boundary crash so far
(`0x800CDAAC`, `0x800CCA6C`, `0x80092748`, `0x800D97EC`, `0x800DD82C`,
`0x800DEB1C`, `0x800EA224`, `0x800F7F6C`) is referenced in the ROM,
either as a literal data word or as a `lui`/`addiu` constant, so that
filter has 8/8 recall. Round 75's extra "starts with a stack-frame
prologue" filter would have *missed* this one (a frameless leaf), so
that refinement is wrong. Separately, a split at a boundary with no
crossing branch and no jump-table target past it is semantically safe
whether or not the piece is a real function: the code after it is
unreachable from the code before it.

## 2026-10-01, round 76: eight-way split of func_800EA14C, plus re-pointing a hook it displaced

Round 75 confirmed. Next idle-at-title crash: `Failed to find function
at 0x800EA224`, inside `func_800EA14C`'s declared `0xae0` range. Seven
clean internal boundaries, no crossing branch. Four jump tables, each
entirely inside its own piece (`jr 0x800EA274` -> `func_800EA224`, `jr
0x800EA7E0` -> `func_800EA714`, `jr 0x800EA914` -> `func_800EA8F4`, `jr
0x800EAAFC` -> `func_800EAA7C`). Split `0xd8` + `0x4f0` + `0x1e0` +
`0x70` + `0x5c` + `0xa8` + `0x14` + `0x1b0`.

New wrinkle: `battletanxga.us.rev0.toml` had a divide-by-zero guard hook
keyed to `func = "func_800EA14C"` at `before_vram = 0x800ea290`, and
that address now belongs to `func_800EA224`. Hooks are looked up by
function name, so it was re-pointed to `func_800EA224`. The hook text
only touches `lo`/`hi` and the registers it divides, so it doesn't
depend on which function it sits in. **Any future split needs this
check:** every `[[patches.hook]]` keyed to the function being split,
with a `before_vram` that falls into a new piece, must move to that
piece's name.

Verified in-sandbox with the submodule's N64Recomp (1349 functions, no
errors). Each switch and the divide guard are emitted in their expected
new functions, and a full build links. Not yet confirmed against a real
run.

## 2026-10-01, round 75: round 73 confirmed; five-way split of func_800DE930

With round 74's build fix, round 73 actually ran and worked: the next
idle-at-title crash moved to `0x800DEB1C`, inside `func_800DE930`'s
declared `0x47c` range. Four clean internal boundaries, no crossing
branch, no jump table, no hooks in range. `0x800DEB1C` opens with a
normal prologue (`addiu $sp, $sp, -0x60`). The three small pieces are
tiny leaf functions (e.g. `andi`/`sltiu`/`jr $ra` flag checks). Split
`0x164` + `0x2c` + `0x38` + `0x24` + `0x290`.

Verified in-sandbox with the submodule's N64Recomp (1342 functions, no
errors, all five pieces plus `func_8009F02C` registered) and a full
build that links. Not yet confirmed against a real run.

`0x800DEB1C` was on round 73's "ROM-referenced split piece" list, so
that filter does catch real ones. Adding "the piece starts with an
`addiu $sp, $sp, -N` prologue" narrows it to 128 pieces in 70 entries,
55 of them in ranges with jump tables. That might be batchable with
automated jump-table and hook checks, but each wrong split would break
currently-working code, so it isn't applied yet.

## 2026-10-01, round 74: round 73 never actually ran -- a new generated file wasn't being compiled, so every build since failed to link

Round 73's retest crashed at the same `0x800DD82C` it fixed. The source
and generated files on Windows were both correct, but the exe was older
than the regenerated `recomp_overlays.inl`, and `cmake --build` was
failing at link: `lld-link: error: undefined symbol: func_8009F02C`,
referenced from `register_overlays.cpp`'s overlay table. Each run since
had been the stale pre-round-73 exe.

Cause: the extra functions from rounds 70-73's splits pushed N64Recomp's
output into a new file, `RecompiledFuncs/funcs_24.c`. N64Recomp writes
`manual_funcs` (round 54's `func_8009F02C`) after all regular functions,
so it landed in that new file. `CMakeLists.txt` collected
`RecompiledFuncs/*.c` with a plain `file(GLOB)`, which only runs at
configure time, so `funcs_24.c` was never compiled into
`RecompiledFuncs.lib`. **Fix:** added `CONFIGURE_DEPENDS` to both
`RecompiledFuncs` globs, so every build re-checks for new generated
files (same as the `src/game/*.cpp` glob already did).

**Sandbox verification gap, also fixed:** this sandbox had been
regenerating with a stale `build/N64Recomp` binary that silently drops
`manual_funcs` entirely (no `func_8009F02C` anywhere in its output), so
its builds never referenced the symbol and never hit this. The
submodule's own `lib/N64ModernRuntime/N64Recomp/build/N64Recomp`
matches the Windows behaviour (1338 functions, `func_8009F02C` in
`funcs_24.c` and in `recomp_overlays.inl`). Use that binary for
in-sandbox verification from now on. Rebuilt with it: `funcs_24.c` is
picked up and compiled with no manual reconfigure, and
`func_8009F02C` is defined in `libRecompiledFuncs.a`. The link succeeds.

## 2026-10-01, round 73: four-way split of func_800DD75C (title-screen idle again)

Idle at the title again: `Failed to find function at 0x800DD82C`,
inside `func_800DD75C`'s declared `0xc18` range. The jr-$ra scan found
three clean internal boundaries. The range has two jump tables, and each
stays entirely inside its own piece: `jr` at `0x800DD878` targets
`0x800DD880`-`0x800DDCAC` (all in `func_800DD82C`), and `jr` at
`0x800DDFD8` targets `0x800DDFE0`-`0x800DE1D8` (all in `func_800DDF04`).
No hooks or instruction patches touch the range. Split `0xd0` + `0x6d8` +
`0x3f8` + `0x78`.

Verified in-sandbox: regenerated (1337 functions, no errors), all three
new functions are registered in `recomp_overlays.inl`, each switch is
emitted in its expected new function, and a full build succeeded. Not
yet confirmed against a real run.

Tried to get ahead of these with a pre-emptive filter: keep only split
pieces whose start address appears in the ROM as a literal word or a
`lui`/`addiu` pair, i.e. something a function pointer could hold. It
still flagged 261 pieces, many of them tiny fragments matching random
data words or targets of jump tables N64Recomp never resolved. That's
not reliable enough to bulk-apply, so these stay one-at-a-time.

## 2026-10-01, round 72: one more merged-function boundary from idling at the title screen

Idle at the title again: `Failed to find function at 0x800D97EC`,
inside `func_800D942C`'s declared `0x4d4` range. One internal `jr $ra`
(`0x800D97E4`), no crossing branch, no jump table, and `0x800D97EC`
opens with a normal prologue (`addiu $sp, $sp, -0x828`). The existing
`[[patches.hook]]` on `func_800D942C` (`before_vram = 0x800d9508`) stays
in the first piece and still lands after regeneration. Split `0x3c0` +
`0x114`.

Verified in-sandbox: regenerated (1334 functions, no errors),
`func_800D97EC` is registered in `recomp_overlays.inl`, and a full build
succeeded. Not yet confirmed against a real run.

## 2026-10-01, round 71: another merged-function boundary, hit by idling at the title screen

Left idle at the title screen with no input (likely the attract/demo
mode kicking in), the game aborted with `Failed to find function at
0x80092748`. That address was inside `func_80091768`'s declared `0x1234`
range. The jr-$ra boundary scan found two internal boundaries
(`0x80092748`, `0x8009277C`), with no branch crossing either. This range
also contains a jump table (`jr` at `0x80092050`); its 15 cases resolve
to `0x80092058`-`0x800920C4`, all inside the first piece, so the split
can't cut a table target off. Split three ways: `0xfe0` + `0x34` +
`0x220` = `0x1234`.

Verified in-sandbox: regenerated (1333 functions, no errors), both new
functions are registered in `recomp_overlays.inl`, and a full build
succeeded. Not yet confirmed against a real run.

A whole-table scan with the same method flags 362 of 1333 entries with
some internal clean boundary, but that count is inflated. Many are real
library functions (`ldiv`, `osYieldThread`, `_Litob`, ...) whose
"second piece" is just alignment-padding `nop`s after the return, and
47 of the 362 contain jump tables that would each need the target check
above. Bulk-splitting isn't safe without filtering those out first, so
these stay one-at-a-time as crashes surface them.

## 2026-10-01, round 70: round 69 worked -- the game renders and the menus play; next crash is another merged-function boundary at the Controller Pak check

**Round 69 confirmed on Windows:** the game runs, renders, and plays
through its menus, the first time it's gotten past boot. `[sp]
osSpTaskStartGo` alternates between the two gfx task buffers
(`0x801293E0`/`0x80129428`) indefinitely, and the throttled diagnostics
keep printing. No audio, as expected (round 58's DSP stub).

It then crashed at the menu step that checks for a Controller Pak:
`Failed to find function at 0x800CCA6C` / `overlays.cpp:368` -- the same
failure as round 64. `0x800CCA6C` sat inside `func_800CC574`'s declared
`0x950` range. A jr-$ra boundary scan of that range (capstone, same
method as rounds 56/59/64) found exactly one internal `jr $ra`
(`0x800CCA64`, delay slot `0x800CCA68`), and no branch crosses it.
`0x800CCA6C` opens with a normal prologue (`addiu $sp, $sp, -0x48; sw
$ra, 0x40($sp)`). Split into `func_800CC574` (`0x4f8`) and
`func_800CCA6C` (`0x458`), sizes summing to `0x950`.

Verified in-sandbox: regenerated (1331 functions, up from 1330, no
errors), `func_800CCA6C` is emitted (`RecompiledFuncs/funcs_13.c`) and
registered in `recomp_overlays.inl`, and a full `ninja
BattleTanxGARecompiled` build succeeded. Not yet confirmed against a
real run.

## 2026-10-01, round 69: the real post-3-frames freeze is func_8007A818's framebuffer wait; rounds 67-68 were wrong and are reverted

Round 68 stopped the crash but the freeze was back, identical to before:
3 gfx tasks, then nothing. A filtered run log showed *every* throttled
once-per-second diagnostic (`[BTGA DEBUG]` from all four yield hooks,
`[BTGA DEBUG v3]` from the VI-dispatch thread) printed exactly once in
a 20-second run -- the whole cooperative scheduler had stalled, not just
one chain. A Break-All debugger sample put the game thread
(`func_8009EEA0 -> func_8009D3A4 -> func_8007A0A0 -> func_8007A818`) at
`RecompiledFuncs/funcs_0.c:6257`.

`func_8007A818` acquires a free framebuffer for the next frame: three
framebuffers (indices 0-2), three "in use" slots at `0xB2/0xB4/0xB6` of
the state struct at `*0x80114500`, result stored to `0xB0`. If all three
are taken, it retries forever (`L_8007A880` -> `beq $v0, $a3,
L_8007A828`) with no OS call -- so ultramodern never switches threads,
the VI-dispatch thread (`func_800A1290` -> `func_800A140C` ->
`func_800A1858`'s `osViSwapBuffer`, which is what frees a framebuffer)
never runs, and its VI messages never even get drained from
`external_messages`. 3 framebuffers == exactly 3 gfx tasks. This is the
same unyielding-poll-loop family as rounds 50/51/53/60 -- and this game
thread is the one earlier rounds kept describing as "varying lines, never
stuck": it was varying lines *inside this loop*. Round 65's yield at
`func_8009D3A4`'s loop-back label never helped because that label is
only reached after `func_8007A818` returns.

**Fix** (`battletanxga.us.rev0.toml`, two hooks on `func_8007A818`):
- `before_vram = 0x8007A888` (the retry branch): yield via
  `btga_yield_via_priority_drop` only when no free buffer was found
  (`ctx->r2 == ctx->r7`, i.e. -1). The loop re-reads `0xB2/0xB4/0xB6`
  every pass, so a yield is sufficient.
- `before_vram = 0x8007A828` (entry wait): `lh $t0, 0xB0` is loaded once
  and `bne $t0, $a3` spins on the stale register -- the round 66
  pattern. Not the observed hang, fixed preemptively: only acts when
  `t0 != -1` (where the original would hang forever), yielding and
  re-reading `0xB0`. A no-op on the retry path, where `t0` is already -1.

**Correction to rounds 67-68 (reverted):** `0x801147E8` is not a gfx
gate and its value is not a throwaway sentinel. It's the pending-audio
`OSTask*` handoff slot: `func_80097844` builds an `M_AUDTASK` OSTask on
its own stack (type field `2` at `sp+0x10`, ucode pointers, sizes) and
posts that address there, then blocks on mq `0x801B4590`;
`func_800A140C` stores it into its `0x200` slot, preempts any running
gfx task with `osSpTaskYield`, and submits it; `func_800976AC` wakes the
audio thread and clears the slot once it's done. With audio stubbed
(round 58), the slot correctly stays 0 -- "no audio task pending". Round
67's `1` "sentinel" got submitted to the SP as a task pointer -- its
crash log literally shows `osSpTaskStartGo(0x00000001)` -- and round 68's
hook would have done the same the first time `func_800976AC` ran. Removed
the `func_800976AC` hook and `src/game/gfx_gate_workaround.cpp`. Round
66's `func_80097844` spin fix stays (correct on its own; unreachable
while audio is stubbed).

Verified in-sandbox: regenerated (1330 functions, no errors), confirmed
both hooks land at `L_8007A828` and right before the `0x8007A888` branch,
no remaining `btga_reopen_gfx_gate` references, and a full `ninja
BattleTanxGARecompiled` build succeeded. `patches.elf` is unchanged from
round 68. Not yet confirmed against a real run -- if this works,
`[BTGA DEBUG]` should now print every second (the retry path calls
`btga_yield_via_priority_drop`, which carries that diagnostic) and
`[sp] osSpTaskStartGo` should keep appearing past 3.

## 2026-10-01, round 68: round 67's fix made the game crash instead of freeze -- reopening the gate every VI tick raced ahead of the SP; retargeted to fire only when the real code clears it

Tested round 67's fix on Windows: the game now runs for a moment (black
window, a handful of ticks of normal startup output) then crashes with an
access violation (`0xc0000005`) instead of freezing. Progress -- the
freeze is gone -- but a new, faster-onset bug replaced it.

Root cause: `btga_reopen_gfx_gate` was called unconditionally from
`func_800A1858`'s RECOMP_PATCH, which fires every real VI tick (~60/sec).
That reopened `0x801147E8` far more often than the real game ever would
have, letting `func_800A140C` build and submit new SP tasks every tick
with none of the backpressure the two-slot pending-task state
(`0x200`/`0x204`, managed by `func_800A15F0`) is designed around --
submitting a new task before the SP had drained the previous ones
corrupts that state, crashing the process almost immediately (only a
single `[sp] osSpTaskStartGo` print before the crash, versus 3 before
round 67).

**Fix:** moved the hook to fire only when the real game actually clears
the flag. `func_800976AC` is the *only* place that ever clears
`0x801147E8` (its one call site is `func_800A15F0`'s own "both task slots
empty" path, `RecompiledFuncs/funcs_8.c:789`) -- i.e. real SP task
completion, which is the correct, naturally-rate-limited pacing signal
for this flag (whatever cadence the SP actually drains tasks at), unlike
a fixed VI-tick timer. `battletanxga.us.rev0.toml` now hooks
`func_800976AC` at `before_vram = 0x800976D0`, immediately after its real
clear (`sw $zero, 0x47E8($at)` at `0x800976CC`), calling
`btga_reopen_gfx_gate` right there -- so the gate reopens at exactly the
same rate the game's own code already decided it was safe to clear it,
instead of racing ahead of the SP. Removed the call from
`patches/recompui_patches.c` and the now-unused `patches/syms.ld` dummy
symbol entry (no longer routed through the ELF patches pipeline at all --
this is a plain `[[patches.hook]]` text splice like the other native
hooks in this file).

Verified in-sandbox: rebuilt `patches.elf` back to its pre-round-67 state
(the reopen call no longer lives there), regenerated via `./build/N64Recomp
patches.toml` then `./build/N64Recomp battletanxga.us.rev0.toml` (no
errors), confirmed `btga_reopen_gfx_gate(rdram, ctx);` now lands only in
`RecompiledFuncs/funcs_5.c` right after `func_800976AC`'s clear (and no
longer in `RecompiledPatches/patches.c`), and a full `cmake ..  && ninja
BattleTanxGARecompiled` build succeeded end-to-end. Not yet confirmed
against a real run.

## 2026-10-01, round 67: found and fixed the real cause of the permanent post-3-frames freeze -- the gfx-task gate (0x801147E8) never reopens because round 58's audio-DSP stub starves its only re-opener

Round 66's fix made no observable difference. Rather than guess again,
added two targeted entry-hook diagnostics (now removed, having served
their purpose): `func_800A15F0` (the "submit pending gfx task, clear the
gate once empty" driver, found via `func_800A1290`'s dispatch table) is
confirmed to run **exactly 3 times then never again**; `func_80097844`
(the only other writer of `0x801147E8`, round 66's fix target) is
confirmed to be **called zero times in a real run**. Since
`func_80097844` is reached exclusively through a 3-entry function-pointer
table (`0x801147EC/F0/F4`, registered once at boot by `func_80097560` ->
`func_800FBDBC` into global `0x8012686C`, confirmed via a raw ROM binary
search for its address since it has no other textual reference anywhere
in `RecompiledFuncs/*.c`), traced every reader of that table.

`func_800FF698` (`RecompiledFuncs/funcs_19.c`) is the audio-processing
loop that reads it: it calls table offset 0 once, then loops calling
offset 4 while `osAiGetLength() & 0x80000000`, and -- the key find --
calls table offset 8 (`func_80097844`) at `funcs_19.c:1440-1445`, but
*only* when a local "did we build any audio commands this pass" flag
(an output param at `$sp+0x20`) is non-zero. That flag is written by
`func_801025C0` (`$a1` out-param, read back right after the call at
`funcs_19.c:1414-1416`) -- which is **round 58's hand-written stub**
(`src/game/func_801025C0_stub.cpp`), chosen back then to unconditionally
write 0 there and short-circuit a real crash deeper in unimplemented
audio-DSP internals (`n_alEnvmixerPull` / `_n_saveBuffer`). That stub was
the right call to avoid the crash, but it has a side effect nobody
connected until now: it permanently starves `func_800FF698`'s only call
site for `func_80097844`, so the gate it would reopen never does.

**Why not just call `func_80097844` directly instead?** Its real argument
is a pointer the caller dereferences at offsets `0x0/0x4/0x8/0xC` before
any gating check runs (`funcs_5.c:5265-5278`) -- in the real call site
it's `&sp[0x10]` from `func_800FF698`'s own frame, which we have no
native equivalent of; calling it with a null or fabricated pointer risks
an out-of-bounds read. But the value it stores into `0x801147E8` on
success (`funcs_5.c:5342` / `:5357-5358`) is just `sp + 0x10` from *its
own* frame -- a transient address that's never dereferenced by any reader
(`func_80097660` only returns it raw, `func_800A140C` only tests it for
non-zero) -- i.e. a non-null sentinel, not real data. Reopening the gate
only needs *some* non-zero value there.

**Fix** (`src/game/gfx_gate_workaround.cpp`, wired into
`patches/recompui_patches.c`'s `func_800A1858` RECOMP_PATCH, same safe
per-VI-tick call boundary as the existing live diagnostic):
`btga_reopen_gfx_gate` checks `0x801147E8` every real VI tick and writes
a `1` sentinel whenever it's found cleared. Runs *after*
`func_800A140C`'s own gate-check for the current tick (it's called from
inside that function, near its end), so it only ever affects the next
tick, never races the current one.

Also removed round 66 parts 3-4's investigative entry-hook diagnostics
(`btga_debug_800A15F0_entry`, `btga_debug_80097844_entry`, and their
`[[patches.hook]]` entries) now that they've served their purpose --
kept the live `gate_0x801147E8` read in `btga_debug_vi_dispatch_live`
since it directly covers verifying this fix (should stop reading 0 once
the gate takes hold), and kept round 66's actual spin-fix (now mostly
moot since this path isn't reached in practice, but still correct in
isolation if it ever is).

Verified in-sandbox: rebuilt `patches.elf` (`cd patches && make
CC=clang LD=ld.lld` -- this sandbox's `make`/`cc` defaults silently
resolve to plain `cc`/`ld`, not `clang`/`ld.lld`, so both must be passed
explicitly here), added the new native symbol to `patches/syms.ld`
(`btga_reopen_gfx_gate = 0x8F0000F0`, next free dummy slot after
`btga_debug_vi_dispatch_live`), regenerated via `./build/N64Recomp
patches.toml` then `./build/N64Recomp battletanxga.us.rev0.toml` (1330
game functions, 60 patch functions, no errors either time), confirmed
`btga_reopen_gfx_gate(rdram, ctx);` lands in
`RecompiledPatches/patches.c`, and a full `cmake .. && ninja
BattleTanxGARecompiled` build succeeded end-to-end including linking.
Not yet confirmed against a real run -- next step is the user pulling,
regenerating, and rebuilding on Windows to see whether the screen keeps
updating past the 3-frame mark.

## 2026-10-01, round 66: round 65's whole investigation was a red herring -- the real deadlock was a fifth unyielding-poll-loop, structurally different from the first four

Round 65 parts 1-2 (the `func_8009D3A4` hook, then the full-queue-drain
fix) made no visible difference -- console output stayed byte-for-byte
identical across both changes, confirmed via multiple fresh clean-terminal
runs. Rather than keep guessing, added live instrumentation directly in
`ultramodern`'s own message-passing code (temporary, submodule-local,
reverted after -- same pattern as the earlier `sp.cpp` gfx-task diagnostic
from round 60): round 65 part 3 confirmed the VI thread keeps attempting
to enqueue a message every tick, continuously, forever (`cur_state->mq`
stays valid at `0x80222930`, never null). Round 65 parts 4-5 then
instrumented `dequeue_external_messages` directly and found the real
picture: delivery to `0x80222930` was **not** stuck after the first
message -- it kept succeeding repeatedly (20+ successful `do_send` calls
to that exact queue over one run, confirmed via an uncapped, targeted log
line). `func_800A1290`'s own repeated "stuck at the same `osRecvMesg`"
debugger samples were never evidence of a real deadlock -- a fast,
healthy event dispatcher that spends nearly all its time idle between
messages looks identical to a stuck one under casual Break-All sampling.
**Rounds 65 parts 1-2's hook and drain-starvation fix are harmless but
were never the actual fix** -- left in place (draining the whole queue in
one pass is a strict improvement regardless), but the real bug was
elsewhere the entire time.

Traced the actual blocker by reading `func_800A140C` (what
`func_800A1290` dispatches to, confirmed earlier) in full: it gates
building a new gfx task on a flag at `0x801147E8`, read via
`func_80097660` and only proceeding if non-zero. Only one other function
(`func_80097844`) ever writes a non-zero value there; `func_800976AC`
clears it back to 0. Reading `func_80097844` found a *fifth*
unyielding-poll-loop-class bug (same family as rounds 50/51/53/60's
`func_80098B40`/`func_800A1384`/`func_80079FF0`), but structurally
different in a way that matters: those three all re-read memory every
iteration (`while (*ptr == 0) {}`), so a bare yield was enough. This one
loads the flag into `$v0` *once* before the loop
(`RecompiledFuncs/funcs_5.c:5332`), then `bne $v0, zero, L_800978D8`
(`:5344`) spins on that stale register copy forever -- never rereading
memory, no OS call inside -- so a plain yield would only turn a
CPU-pegging infinite loop into a yielding-but-still-infinite one. This
function only reaches the spin if called while the flag is already
non-zero (a race the cooperative, one-thread-at-a-time scheduler can
expose even where real N64 hardware's actual RSP/CPU timing apparently
never did).

**Fix:** `battletanxga.us.rev0.toml`'s hook at this loop (`func_80097844`,
`before_vram = 0x800978D8`) does two things, not one: calls
`btga_yield_via_priority_drop` (to actually let `func_800976AC`'s thread
run and clear the flag), *and* recomputes `ctx->r2` via the exact same
`lui`+`lw` the loop's own entry used
(`ctx->r2 = MEM_W(S32(0x8011 << 16), 0x47E8);`), so the loop can actually
observe the flag once it clears instead of checking a permanently-stale
copy.

Verified: regenerated via the local `N64Recomp` (1330 functions, no
errors), confirmed the hook lands exactly at `L_800978D8`
(`RecompiledFuncs/funcs_5.c:5341-5342`), and a full `cmake . && ninja
BattleTanxGARecompiled` build succeeded end-to-end including linking. Not
yet confirmed against a real run.

## 2026-09-30, round 65 (part 2): the round 65 hook unblocked func_800A1290 exactly once, then re-stuck -- root cause was single-message drain starvation, not a missing hook

Round 65's new hook (func_8009D3A4's loop-back label) did have a real
effect on a live run: `func_800A1858`'s own diagnostic
(`[BTGA DEBUG v3]`) printed once where it had printed zero times before --
proof `func_800A1290`'s chain genuinely ran once more. But it then went
straight back to being permanently parked at the exact same `osRecvMesg`
(confirmed via a flagged-thread Continue+Break-All check), and the
diagnostic never printed again even after a patient real-time wait.

Checked `events_context.vi`'s actual live state via the debugger (raw
struct field access -- `events_context.vi.states[events_context.vi.cur_state].mq`
and `...retrace_count`, *not* `get_cur_state()`, which is a real C++
method: evaluating it as a live function call while the app's many
cooperative-scheduler threads were paused triggered "An attempt to abort
the evaluation failed. The process is now in an indeterminate state." --
had to fully stop debugging and confirm no orphaned process was left in
Task Manager before continuing). Confirmed `mq` correctly resolves to
`0x80222930` (a real, valid, non-null pointer -- not the "never
initialized" scenario that would've been the simpler explanation) and
`retrace_count=1`, meaning ultramodern's VI thread (`ultramodern/src/
events.cpp:187`) really should be enqueueing a fresh message for this
queue on every single VI tick, not just once.

Reading `ultramodern/src/mesgqueue.cpp` found the actual cause:
`external_messages` is one shared FIFO (`moodycamel::BlockingConcurrentQueue`)
fed by *every* event source -- VI, AI, SP, DP, Timer, SI all call
`enqueue_external_message_src` into the same queue. `yield_self_1ms`
(what `btga_yield_via_priority_drop` was calling) only pops a single
entry per call (`wait_for_external_message_timed`, `mesgqueue.cpp:61-68`).
If any other source produces faster than our once-per-Game2-loop-
iteration drain rate, the specific VI message `func_800A1290` is waiting
for can end up stuck arbitrarily far back in FIFO order behind a growing
backlog of unrelated messages -- fully explaining one lucky early
delivery (round 65's hook happened to run before any backlog existed)
followed by permanent starvation once one built up.

**Fix:** `btga_yield_via_priority_drop` (`src/main/scheduler_workaround.cpp`)
now calls `dequeue_external_messages` (a plain, non-`extern "C"` but
ordinarily-linkable C++ function already defined in `mesgqueue.cpp:40`,
just never exposed via a header -- declared a matching prototype directly)
instead of `yield_self_1ms`. That function drains the *entire* queue in
one pass (`while (external_messages.try_dequeue(...))`) rather than one
entry, so it can't starve this way regardless of relative production
rates between event sources. This is a strict improvement for the three
existing hook sites too (`func_80098B40`, `func_800A1384` x2,
`func_80079FF0`), not just the new one -- draining more thoroughly can
only help them, never hurt.

Verified: full `ninja BattleTanxGARecompiled` build succeeded end-to-end,
including linking against `dequeue_external_messages` with a
hand-declared prototype (confirms the C++ name-mangling matches the real
symbol). Not yet confirmed against a real run.

## 2026-09-30, round 65: a fourth unyielding-poll-loop-class deadlock, this time a starved external-message drain -- fixed by moving the existing yield hook to code the game actually still runs

Round 64's merged-function fix got the game past `0x800CDAAC`, but the
screen went straight back to a deterministic black-screen freeze (same
three `[sp] Gfx task` lines every run, then silence -- confirmed via a
patient real-time wait, not just a debugger pause artifact). Added a live
diagnostic (`src/game/vi_dispatch_diag.cpp`, called every real VI tick from
the `func_800A1858` `RECOMP_PATCH`) to get an unbiased read on `mq
0x80222930`'s state, since round 52's existing diagnostic
(`btga_debug_check_vi_dispatch`) only ever fires from inside the old
`func_800A1384`/`func_80079FF0` spin-loop hooks the game no longer
revisits -- its silence was never actually informative about this freeze.
The new diagnostic printed *zero* times over a full minute, meaning
`func_800A1858` itself had stopped being called entirely -- the whole
`func_800A1290` (VI-message dispatch) -> `func_800A140C` -> `func_800A1858`
chain had halted, while the game's *other* independent per-frame chain
(`func_8009EEA0` -> `func_8009D3A4` -> `func_8007A0A0` -> `func_8007A818`,
"Game 2" in the debugger) stayed confirmed healthy throughout (repeated
Continue+Break-All samples kept landing on different lines -- verified
`func_8007A818`'s own suspicious-looking loop from round 63 can't actually
be an infinite spin either, since it's bounded by an unconditionally-
incrementing `slti ..., 0x3` index check that must exit within 4
iterations regardless of memory contents; that was a red herring, not a
bug).

Debugger confirmed (flagging the thread row to track it precisely across
Continue+Break-All cycles, since two threads share the "Game 3" label and
are easy to mix up) that `func_800A1290`'s thread was genuinely and
permanently parked at its very first `osRecvMesg`, not merely idle between
messages. Root cause: the same drain gap documented at
`btga_yield_via_priority_drop`'s own definition
(`src/main/scheduler_workaround.cpp`) and round 52's notes below --
ultramodern's VI thread only enqueues into an intermediate
`external_messages` queue; delivery into a real `OSMesgQueue` (waking a
blocked `osRecvMesg`) only happens when *some* game thread calls
`yield_self_1ms`/`wait_for_external_message`. The three existing yield
hooks (`func_80098B40`, `func_800A1384`, `func_80079FF0` -- rounds 50/51/53/
60) provided that drain as a side effect of fixing their own unrelated
spins, but round 64's fix let the game advance past all three of those
functions entirely, so nothing was left draining the queue at all once
that happened.

**Fix:** moved (really, added a fourth instance of) the same
`btga_yield_via_priority_drop` hook to `func_8009D3A4`'s own loop-back
label (`L_8009D3B8`) -- the per-iteration re-entry point of the game's
*other*, still-healthy main per-frame loop (`func_8007A818`'s thread).
This is explicitly not a repeat of round 61's mistake:
`btga_yield_via_priority_drop` takes only `rdram`, never touches `ctx`, so
unlike `recomp_run_ui_callbacks` (which manipulates `ctx` and invokes
arbitrary game code via `LOOKUP_FUNC` using that same `ctx`) it can't
corrupt whatever register-resident locals the hooked function's own logic
is using -- the exact property that already made the three earlier yield
hooks safe via `[[patches.hook]]` throughout rounds 50-60. (A short-lived
detour: first tried wiring the drain into the `func_800A1858`
`RECOMP_PATCH` itself, before realizing that function is *downstream* of
the very thread that's stuck -- if `func_800A1290` never wakes, it never
dispatches down to `func_800A1858`, so a fix placed there would never
execute. Reverted that attempt; the round 65 diagnostic function stays, and
should start actually printing once this real fix lets the chain run
again.)

Verified: regenerated via the local `N64Recomp` (1330 functions, no
errors), confirmed the hook lands exactly at `L_8009D3B8` in
`RecompiledFuncs/funcs_6.c`, and a full `cmake . && ninja
BattleTanxGARecompiled` build succeeded end-to-end including linking. Not
yet confirmed against a real run.

## 2026-09-30, round 64: the "render freeze" wasn't a render bug -- it was a tenth merged-function boundary the game hadn't reached until round 62's fix let it

Round 63 ended with a live theory (RT64 HLE gap, or the documented USA
boot-timing race) for why the screen stayed frozen despite healthy game
logic and continuous gfx task submission. Turned out to be neither.

While setting a real breakpoint to check round 63's spin-loop suspicion in
`func_8007A818` (`RecompiledFuncs/funcs_0.c:6198`), continuing past it let
real time pass -- and the game's own boot sequence, now actually able to
progress thanks to round 62's `recomp_run_ui_callbacks` fix, ran far enough
to hit a genuinely new crash: `Failed to find function at 0x800CDAAC` /
`Assertion failed: false, ... librecomp/src/overlays.cpp, line 368`
(librecomp's `abort()` path for an unresolved indirect call) -- the exact
same failure mode as every prior merged-function-boundary bug this project
has fixed (rounds 39-46, 54-56, 59). The "frozen screen" was never a
rendering bug at all: the game was stuck *earlier*, in a state that never
called this address, until round 62 let it advance far enough to actually
reach and crash on it. Round 63's `func_8007A818`/USA-boot-race/RT64
theories are retired as red herrings -- noted here so no one re-investigates
them.

`func_800CD970` (declared `0x400` bytes) turned out to be twelve functions
back to back, with `0x800CDAAC` (the crashing indirect-call target) as the
third. Verified with the same automated `jr $ra`-boundary scan used for
rounds 56/59's multi-way splits (parse every instruction address, propose a
boundary right after each `jr $ra` + delay slot, confirm no branch/jump
crosses a proposed boundary) -- clean with no violations, sizes summing to
exactly `0x400`. Fixed via the normal `syms.toml` split (twelve entries
replacing one, `BattleTanxGASyms/battletanxga.us.rev0.syms.toml`), no
`manual_funcs`/`ignored` needed. Verified: regenerated via the local
`N64Recomp` (1330 functions, no errors), confirmed all eleven new functions
compile separately (`RecompiledFuncs/funcs_13.c:4846` on), and a full
`cmake . && ninja BattleTanxGARecompiled` build succeeded end-to-end
including linking. Not yet confirmed against a real run.

## 2026-09-30, round 63: round 62's fix confirmed working correctly, but a separate, pre-existing render-freeze bug is now the actual blocker

First, real Windows toolchain setup problems had to be solved before round 62
could even be tested (all fixed, documented here since they'll recur for
anyone else setting this up): neither Visual Studio's bundled "C++ Clang
Compiler for Windows" nor the official llvm.org Windows installer include
the MIPS backend (`clang -print-targets` lists aarch64/arm/x86/riscv/wasm/
bpf/nvptx on both, no mips) -- fixed by routing the `patches/` build through
WSL specifically on Windows (`CMakeLists.txt`, `PATCHES_MAKE_COMMAND`).
Separately, live debugging the running game needed: (1) `Debug -> Attach to
Process` with the code type explicitly forced to **Native** (it was
defaulting to "Automatically determine," which silently produced a
non-functional debug session -- empty call stacks and "the current frame
does not support evaluating expressions" for *every* thread, not just the
one being investigated); (2) a genuine Debug build (`CMAKE_BUILD_TYPE`
was cached as `Release` from early in this project's history --
`cmake -B build -DCMAKE_BUILD_TYPE=Debug` was needed before the debugger
could unwind any stack at all, confirmed via Debug -> Windows -> Modules
showing "Binary was not built with debug information" beforehand).

**Round 62's fix is confirmed correct on a real run**, via multiple
independent signals: the console's `[BTGA DEBUG]` print now shows
`is_game_started=1` (previously never seen), and behavior is no longer
deterministic run-to-run in the way a permanently-inert callback queue
would produce -- one run reached and got stuck on the static 3DO
publisher splash image (never seen before this fix), other runs still show
plain black. This is consistent with round 61's theory being *right* that
`recomp_run_ui_callbacks` needing to fire is what let the "start game"
click's queued transition actually happen for the first time, genuinely
advancing the boot sequence past where it could ever previously reach.

**But the underlying frozen-screen symptom itself persists**, and a live
debugging session (once the toolchain issues above were fixed) shows this
is a *different*, likely pre-existing bug, not something round 62
introduced or something UI-callback-shaped:
- The real per-frame game-logic thread (`func_8009EEA0` -> `func_8009D3A4`
  -> `func_8007A0A0` -> `func_8007A818`) is confirmed healthy, not
  deadlocked -- repeated Continue+Break-All samples show it genuinely
  executing different lines each time, same as the established-good
  baseline from rounds 50-60.
- `[sp] osSpTaskStartGo`/`Gfx task` prints (the same temporary diagnostic
  from round 60) confirm real gfx tasks are still being submitted
  continuously while frozen. **This is not new evidence of health** --
  the exact same signature (continuous submission, frozen screen) was
  already the confirmed state in round 61 *before* today's fix, so this
  alone doesn't distinguish "boot stuck" from "real gameplay stuck." The
  two repeating buffer addresses (`0x801293E0`/`0x80129428`) are most
  likely just this game's fixed pair of framebuffers reused for every
  frame regardless of content -- their repetition isn't diagnostic of
  whether the visible picture is actually changing.
- No GPU driver TDR/reset in Windows Event Viewer, no startup warnings
  from the graphics backend, and the host's SDL event loop is confirmed
  genuinely alive (USB controller hot-plug/unplug is detected and logged
  live while the screen is frozen) -- rules out a full application hang
  or a GPU device-removed scenario.
- `RT64::PresentQueue::threadLoop()`'s own internals aren't visible in the
  debugger (collapsed under "[External Code]" -- RT64 likely lacks debug
  info regardless of this project's own `CMAKE_BUILD_TYPE`). `rt64.log`
  doesn't get created by this project's integration at all (that log path
  is wired up in `RT64::Application`'s own standalone-player code path,
  which this project's `lib/RecompFrontend/recompui/src/renderer/
  rt64_render_context.cpp` integration doesn't appear to use), so that
  avenue for RT64-side diagnostics is a dead end as currently wired.
- `func_8007A818` (`RecompiledFuncs/funcs_0.c:6186`) has a real,
  suspicious-looking construct right at its start (`0x8007A824`-
  `0x8007A828`): `$t0` is loaded from `[$a1+0xB0]` exactly once, then a
  `bne $t0, $a3, L_8007A828` loop branches back to *itself* with `$t0`
  never reloaded inside the loop -- a genuinely unconditional infinite
  spin if `$t0 != -1` (`$a3`) the first time it's checked, same general
  shape as the scheduler-gap bugs fixed in rounds 50-53/60. **Not yet
  confirmed this is actually being hit** -- the function's own varying
  line numbers across breaks are equally explained by its other,
  genuinely-bounded logic below this point (a small fixed-iteration
  search over up to 3 fields at `+0xB2`/`+0xB4`/`+0xB6`, repeated for what
  looks like multiple controller-status channels). Needs a real breakpoint
  at `0x8007A828` specifically (not just sampled Break-All snapshots) to
  confirm whether it's ever actually entered during the freeze, before
  concluding this is the cause rather than a red herring.

**Leading theories for next session, not yet tested:**
1. The already-documented USA boot-timing race (`patches/README.md`) --
   `osContInit` called before libultra's VI-timer list finishes
   initializing, per two independent emulator projects' own writeups
   (n64js PR #123, mupen64plus-core issue #283). The "stuck exactly on the
   boot/publisher splash" symptom matches this well. Not yet located in
   this ROM's own symbol map (same status as when `patches/README.md` was
   originally written) -- next step would be finding the real equivalent
   of n64js's patched branch (right after the IPL3 checksum check) in this
   game's actual boot code, which the resident-code addresses established
   very early in this project (`syms/rom_info.md`) should make tractable
   now that real symbols/disassembly exist for far more of the ROM than
   when that README note was first written.
2. RT64's HLE graphics-command interpreter failing to correctly process
   something in this game's specific display lists (an unusual GBI/F3D
   microcode variant or command sequence), silently leaving the
   framebuffer showing whatever was last successfully drawn while still
   "succeeding" at the task-submission level every frame. Harder to
   pursue without GPU-level tooling (RenderDoc/PIX) this session didn't
   attempt.

Either way: **round 62's actual deliverable (the ELF patches toolchain and
the `recomp_run_ui_callbacks` wiring) should be considered done and
correct** -- what's left is a distinct, likely pre-existing rendering/boot
bug that happened to be masked by the UI-callback gap until now.

## 2026-09-29, round 62: stood up the ELF-based `patches/` toolchain and properly wired `recomp_run_ui_callbacks` via a real RECOMP_PATCH

Round 61's `[[patches.hook]]` shortcut was reverted for causing a worse
regression (a real Windows app hang instead of merely-unresponsive UI) --
see that entry below for the full root cause. This round does it properly:
a complete, minimal version of BanjoRecomp's ELF-based patch pipeline
(cross-compile real C to MIPS object code via clang, link it with
`ld.lld` against dummy absolute-address symbols, then run it back through
N64Recomp as a *second*, separate input), built from scratch for this
project rather than copied wholesale (BanjoRecomp's own `patches.h`
depends on a vendored `bk-decomp` SDK-headers submodule this project
doesn't have).

New files, all under `patches/` unless noted:
- `patches.h`: `RECOMP_PATCH`/`RECOMP_EXPORT` section attributes, the
  `osViBlack`/`osViSwapBuffer` -> `_recomp` renames patch code needs to
  call the stock runtime, and a `typedef int bool` shim for the freestanding
  MIPS build (no `<stdbool.h>` under `-nostdinc`) that the already-existing
  `recompui_event_structs.h` needs.
- `patch_helpers.h`: `DECLARE_FUNC`, which gives one declaration valid on
  both sides of the ABI -- a plain prototype when compiled as real MIPS
  (`-DMIPS`, this directory's own build), the real recompiled-function
  signature `(uint8_t* rdram, recomp_context* ctx)` otherwise (host C++).
- `ui_funcs.h` (already existed as a placeholder for recompui's "forced
  game includes" hook): added the actual `DECLARE_FUNC(void,
  recomp_run_ui_callbacks);` declaration, now that there's a real patch to
  use it.
- `recompui_patches.c`: `RECOMP_PATCH void func_800A1858(...)` -- see
  below.
- `patches.toml` (project root): the second N64Recomp input, `elf_path`
  instead of `rom_file_path`/`symbols_file_path`, `func_reference_syms_file`
  pointed at the same `BattleTanxGASyms/battletanxga.us.rev0.syms.toml` the
  main recompile uses so patch code can call original game functions by
  name.
- `Makefile`, `patches.ld`, `syms.ld`, `include/PR/*.h` already existed
  from round 22's scaffolding (`PROGRESS.md` item 8) and needed no changes
  -- `syms.ld` already had dummy addresses reserved for
  `recomp_run_ui_callbacks`, `osViBlack_recomp` and `osViSwapBuffer_recomp`
  specifically, confirming this was anticipated back then.

**Patch target:** `func_800A1858` (`RecompiledFuncs/funcs_8.c:1191`, real
address confirmed in the syms table at `0x800A1858`, size `0x78`) -- this
game's VI-swap-throttle routine, reached once per real VI tick
(`func_800A1290`'s VI-message dispatch -> `func_800A140C` -> here, traced
back in round 52) and the only place that calls `osViSwapBuffer`. Fully
reimplemented (not stubbed) from the disassembly: `recomp_run_ui_callbacks()`
unconditionally first, then the original's real logic -- an early return if
a `u16` "pending swap count" hasn't reached a `u16` "threshold" (both raw
offsets into the same state pointer the original indexed, `0x1F0`/`0x1EC` --
no recovered struct type for this yet, so read via `u16*` pointer casts
matching the disassembly exactly rather than guessed field names), then
`func_8007AF84(1)` (an original, un-patched game function, called by name --
resolved through `func_reference_syms_file` like BanjoRecomp's patches call
their own original functions), a conditional `osViBlack(0)` gated on a
one-shot `0x1F2` flag, and finally `osViSwapBuffer(...)` and clearing the
pending-swap counter.

**Why this function and not another `[[patches.hook]]` spot:** a
`RECOMP_PATCH` fully *replaces* a function at its real call boundary (the
existing `jal 0x800A1858` sites already save/restore their own registers
around a genuine call, same as any other function call) instead of
splicing raw text into the *middle* of a function's body with no register
preservation -- exactly the distinction round 61's regression came from.

**Duplicate-symbol snag and why it's not a real problem:** the ROM's own
recompile (`RecompiledFuncs/funcs_8.c`) still generates its own
`func_800A1858` body too (nothing in `battletanxga.us.rev0.toml` tells it
to skip that address) -- N64Recomp's `RECOMP_PATCH` mechanism relies on
`recomp.h`'s `RECOMP_FUNC` being a *weak* symbol so the linker silently
keeps whichever definition it sees first (`PatchesLib` is listed before
`RecompiledFuncs` in `CMakeLists.txt`'s link line already, from round 22).
That's only true for `recomp.h`'s Clang branch (`extern inline
__attribute__((weak,noinline))`) -- the GCC branch (`__attribute__((noipa,
...))`) has no weak linkage, so a build configured with plain GCC hits a
hard `multiple definition of 'func_800A1858'` linker error. Confirmed this
sandbox's default `cmake` configure silently picked up GCC and hit exactly
that error; not a real project regression, since `CMakeLists.txt` already
requires clang-cl specifically on Windows (see its own top-of-file NOTE)
and this project has never targeted plain MSVC `cl.exe` or GCC on Linux as
supported toolchains. Reconfigured a clean build with
`-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` to match the
project's actual required toolchain and confirmed: `PatchesLib` builds,
the full `BattleTanxGARecompiled` link succeeds, and disassembling the
final binary's `func_800A1858` symbol shows it resolved to the patch's
version (calls `recomp_run_ui_callbacks`, `func_8007AF84`, `osViBlack_recomp`,
`osViSwapBuffer_recomp` in exactly the order the patch source has them --
not the original's own body). Not yet confirmed against a real run --
that needs the user's Windows machine (clang-cl + `ld.lld`/GNU `make`, or
an equivalent driving the same three `patches/` build commands, need to be
available there; see PROGRESS.md for what to confirm next).

## 2026-09-29, round 61 (REVERTED): recompui's per-frame UI callback pump was never wired up; a same-context mid-body injection made things worse, not better

Round 60's fix cleared the last known unyielding poll loop. The game now
boots, the menu works, and clicking into the game genuinely runs without
crashing -- confirmed via debugger: the main game-logic thread cycles
through many different functions each sample (not stuck anywhere), CPU
sits at a steady ~10% (one core, matching normal per-frame work, not a
spin), and `[sp] osSpTaskStartGo`/`Gfx task` prints (enabled via a
temporary local diagnostic in `librecomp/src/sp.cpp`, reverted after
verifying) confirmed the game is genuinely submitting real display lists
every frame, alternating between two buffers as expected.

But the screen goes solid black once in-game and never changes, and
neither clicking around nor keypresses (Escape/Enter) do anything --
including the window's own close button, which normally still works fine
pre-game-start (confirmed: `SDL_QUIT`'s handler,
`lib/RecompFrontend/recompinput/src/input_events.cpp:107-116`, calls
`ultramodern::quit()` directly before the game has started, but opens a
`recompui::open_quit_game_prompt()` confirmation dialog afterward --
which never visibly appears or responds to any input once the game is
running).

Traced this to `recomp_run_ui_callbacks`
(`lib/RecompFrontend/recompui/src/api/ui_api_events.cpp:102`) -- the
function that pumps/renders recompui's UI layer (buttons, dialogs, text)
each frame -- never being called anywhere in this project. Confirmed via
BanjoRecomp (the reference project used throughout this whole session)
that this is a required wiring step, not something that happens
automatically: Banjo's `patches/recompui_patches.c` `RECOMP_PATCH`es a
per-frame game function specifically to call it, via Banjo's full
ELF-based `patches/` toolchain (which this project has never set up --
`patches/` has no sources, `patches.toml` doesn't exist; see this file's
own `[patches]` section notes). Without this call ever happening, the
main menu's UI apparently renders through a separate, simpler path during
startup (menu interaction was confirmed working many rounds ago), but
nothing UI-related -- including the quit-confirmation prompt -- ever gets
pumped again once real gameplay's own per-frame loop takes over.

**Fix:** rather than standing up Banjo's full ELF-based patch toolchain
from scratch, used the already-proven `[[patches.hook]]` mechanism to
inject the same call directly into the game's own main per-frame loop --
`func_8009D3A4` (entry point `func_8009EEA0`, this whole session's main
loop thread), at its own loop-back label `L_8009D3B8` (confirmed reached
exactly once per iteration via its own `goto L_8009D3B8`,
`funcs_6.c:12890-12893`). `battletanxga.us.rev0.toml`:
```
[[patches.hook]]
func = "func_8009D3A4"
before_vram = 0x8009D3B8
text = """
    { extern void recomp_run_ui_callbacks(uint8_t *rdram, recomp_context *ctx); recomp_run_ui_callbacks(rdram, ctx); }
"""
```
Verified: regenerated via the local `N64RecompCLI`, confirmed the hook
lands correctly at the loop label, and a full `cmake -S . -B build &&
ninja BattleTanxGARecompiled` build succeeded end-to-end including
linking (confirming `recomp_run_ui_callbacks` resolves against
RecompFrontend/recompui).

**On a real run: made things worse, not better.** The screen behaved the
same (black, unresponsive), but Windows now flagged a genuine app hang
(`Hang type: Top level window is idle`) where before it had been merely
unresponsive-but-stable (no hang flagged, steady ~10% CPU, message pump
confirmed running). Root cause: `recomp_run_ui_callbacks` can call
`LOOKUP_FUNC(cur_callback.callback.callback)(rdram, ctx)` -- executing
whatever real, compiled game function is queued as a UI callback (e.g.
the quit prompt's button handlers), using the *same* `ctx` as whatever's
mid-execution at the call site. `[[patches.hook]]` splices raw C text into
the *middle* of an existing function's body -- it is not a real call
boundary with compiler-managed register save/restore -- so nothing
protects `func_8009D3A4`'s own register-resident locals (`s0`-`s7`, used
throughout its real per-frame logic) from being clobbered by whatever a
fired callback does. **Reverted** (`battletanxga.us.rev0.toml`, replaced
with an explanatory comment in place of the hook).

Real fix likely needs BanjoRecomp's actual approach: a full `RECOMP_PATCH`
(complete function *replacement*, which gets proper register preservation
at its own genuine call boundary, since the call site it's inserted at was
already a real `jal` in the original code) via the ELF-based `patches/`
toolchain this project has never set up (`patches/` has no sources,
`patches.toml` doesn't exist) -- not a same-context mid-body injection via
`[[patches.hook]]`. Setting that toolchain up (a cross-compiling
clang+ld.lld pipeline, `patches.toml`, `syms.ld`, etc., matching
BanjoRecomp's own `patches/` directory) is a real, scoped next step for
whoever picks this up, distinct from everything else in this session.

## 2026-09-28, round 60: third unyielding poll loop found, same fix applied

After rounds 50-59 cleared every downstream blocker, the same thread
(entry point `func_8009EEA0`) progressed further and hit a third bare
unyielding poll loop -- same class as `func_80098B40`/`func_800A1384`:
`func_80079FF0` is a plain `while (*(int32_t*)(0x800114F8 + 0xC4) == 0)
{}` (its own loop label `L_8007A014`), no OS call inside it at all.
Applied the same proven fix: a `[[patches.hook]]` calling
`btga_yield_via_priority_drop` at the loop's own label. Verified:
regenerated via the local `N64RecompCLI`, confirmed the hook lands
correctly right after the label, and a full `ninja BattleTanxGARecompiled`
build succeeded end-to-end. Not yet confirmed against a real run.

## 2026-09-28, round 59: ninth confirmed merged-function boundary -- a seven-way split

Round 58's audio-chain short-circuit worked (no more access violations
there). Next crash: `Failed to find function at 0x800BFD40`.
`func_800BFCA4` (declared `0x1f0`) turned out to be seven small functions
back to back -- itself plus six tiny accessor-style functions (sizes
`0x24`/`0x20`/`0x10`/`0x10`/`0xa8`/`0x48`). Verified with the same
automated approach as round 56's three-way split (every `jr $ra` found,
boundaries proposed right after each, confirmed no branch/jump crosses
any proposed boundary), scaled up for a 7-way split. Sizes sum to
`0x1f0`, matching the original exactly. Fixed via the normal `syms.toml`
split, no `manual_funcs`/`ignored` needed. Verified: regenerated via the
local `N64RecompCLI`, confirmed all seven functions compile separately,
and a full `ninja BattleTanxGARecompiled` build succeeded end-to-end. Not
yet confirmed against a real run.

## 2026-09-28, round 58: short-circuited the whole audio-command-list chain at its entry point instead of chasing internal DSP crashes one at a time

Round 57's `n_alEnvmixerPull` fix (pass the output pointer through
unchanged) wasn't enough by itself: the very next test hit another access
violation, one call deeper, inside `_n_saveBuffer` -- a fully real,
successfully-decompiled function (not a stub), part of the same call
chain: `func_801025C0` -> `func_801028B0` -> `func_80102900` -> a
per-voice-type indirect dispatch -> `func_80101320` ->
`n_alEnvmixerPull`/`_n_saveBuffer`. Every step in this chain follows the
same pattern -- write a typed command header, advance a running buffer
pointer, return the new position for the next step -- building what's
almost certainly an RSP audio command list. Continuing to patch each
internal function's pointer arithmetic one crash at a time risked an
open-ended number of further crashes in genuinely undocumented,
bit-packed DSP internals this project has no real implementation of.

Instead, found and used the chain's own existing "nothing to do" contract
at its entry point. Disassembly of `func_801025C0` shows an early check
that, when false, just writes `0` to an output parameter (`a1`, a pointer
to a caller-local "command count") and returns -- and its caller
(`RecompiledFuncs/funcs_18.c`, around the `0x800FF788` call site) reads
that count back afterward and skips submitting anything further when it's
`0`. This isn't a guessed replacement -- it's the game's own real
no-audio-this-frame behavior, confirmed by reading both sides of the call.

**Fix:** added `func_801025C0` to the `ignored` list alongside
`n_alEnvmixerPull` (`battletanxga.us.rev0.toml`), and wrote
`src/game/func_801025C0_stub.cpp`: `extern "C" void func_801025C0(...) {
MEM_W(0, ctx->r5) = 0; }` -- unconditionally takes the real "nothing to
process" path rather than only when the original condition happened to be
false. Result: no audio commands get built this frame (silence, same as
round 57's gap), but the entire deep DSP chain underneath it is bypassed
rather than needing to be individually verified safe. Verified: regenerated
via the local `N64RecompCLI`, confirmed `func_801025C0` no longer has a
generated body (only the call site remains, matching the `ignored`
pattern), and a full `cmake -S . -B build && ninja BattleTanxGARecompiled`
build succeeded end-to-end including linking. Not yet confirmed against a
real run.

## 2026-09-28, round 57: real crash (access violation) -- the long-known n_alEnvmixerPull audio gap finally got exercised; fixed by switching it from `stubbed` to `ignored` with a hand-written pass-through

Round 56's fix got the game past every "Failed to find function" merged-
boundary crash and into a genuinely different failure: a Windows access
violation (`0xc0000005`) inside `func_80101320` (the very function round 56
just split out), caught live via the debugger at
`MEM_W(0X0, ctx->r3) = ctx->r7;` -- a write through a corrupted pointer.

Traced the corruption back two call levels: `func_80101320` calls
`func_800FFA90`, which loops calling `n_alEnvmixerPull` and uses its
return value (`$v0`/`ctx->r2`) as an advanced output-buffer pointer for
the next iteration (and as its own return value). `n_alEnvmixerPull` has
been in the `stubs` list since round 21-22 (N64Recomp can't statically
decompile it -- a computed jump table whose size it can't determine) --
but a `stubs` entry generates a completely *empty* function body that
touches nothing, so `$v0` after the "call" just retains whatever it held
beforehand (a small loop-count integer from earlier in `func_800FFA90`,
not a pointer). That stale garbage value propagates up as the return
value and gets used as a write address two calls later, crashing. Round
21-22's own note already flagged this as "a real gap to revisit once real
audio is being worked on, not dead code like the others in this list" --
this is exactly that moment, now that the audio thread's real processing
loop is actually being reached.

**Fix:** moved `n_alEnvmixerPull` out of `stubs` and into a new `ignored`
list (`battletanxga.us.rev0.toml`'s `[patches]` table) -- unlike
`stubbed`, an `ignored` function skips N64Recomp's decompilation (and the
jump-table analysis that made it unanalyzable) entirely, expecting the
project to supply its own native implementation. Added
`src/game/n_alEnvmixerPull_stub.cpp`: `extern "C" void n_alEnvmixerPull(...)
{ ctx->r2 = ctx->r6; }` -- passes the output-buffer pointer through
unchanged (silence/no mixing instead of real audio, but the pointer chain
stays valid, matching "wrote zero bytes" rather than a guessed-and-possibly-
wrong advance amount). This is purely project-owned code, no submodule
patch needed this time. `func_801000B0` (round 22's other stub, sharing
the same unanalyzable jump table) is left untouched in `stubs` -- it's
reached only via a direct branch-into-interior from within
`n_alEnvmixerPull`'s own byte range, a different situation not currently
exercised by any live crash.

Verified: regenerated via the local `N64RecompCLI` (no jump-table error
this time, confirming `ignored` genuinely skips that analysis), confirmed
`n_alEnvmixerPull` no longer has a generated body/declaration
(`RecompiledFuncs/funcs.h`, `funcs_19.c` -- only the call site remains),
and a full `cmake -S . -B build && ninja BattleTanxGARecompiled` build
succeeded end-to-end, including linking the final executable (confirming
the hand-written symbol resolves correctly against the implicit-
declaration call site in the generated C). Not yet confirmed against a
real run.

## 2026-09-28, round 56: eighth confirmed merged-function boundary -- this one a three-way split

Next crash after round 55's fix: `Failed to find function at 0x80101320`.
`n_alResampleParam` (declared `0x524`) turned out to be *three* clean
functions back to back: itself (`0x24`, a tiny trampoline that just calls
another function and returns 0), an unnamed `func_80101320` (`0x310`, the
runtime's actual failing lookup target), and an unnamed `func_80101630`
(`0x1f0`, ending exactly at the original declared boundary with no
leftover padding). `0x24+0x310+0x1f0 = 0x524`, matching the original
exactly. Verified with an automated boundary-crossing scan (parsed every
`beq`/`bne`/`bgez`/etc. and `j` target in the full disassembly range and
confirmed none crosses from one proposed function into another) rather
than eyeballing it, given this was a 3-way split instead of the usual 2.
Fixed via the normal `syms.toml` split, no `manual_funcs` needed. Verified:
regenerated via the local `N64RecompCLI`, confirmed all three functions
compile separately (`RecompiledFuncs/funcs_19.c:3887`, `:3915`, `:4401`),
and a full `ninja BattleTanxGARecompiled` build succeeded end-to-end. Not
yet confirmed against a real run.

## 2026-09-28, round 55: seventh confirmed merged-function boundary, same class as rounds 39-46

Round 54's `func_8009F02C` `manual_funcs` fix confirmed working (that
specific crash stopped happening). Next crash: `Failed to find function at
0x800FC02C`. Unlike round 54, this one *is* the ordinary merged-function
pattern: `player_text_1AE0` (declared `0x388`, an n64sym signature-match
name per the syms.toml file's own caveat that such names aren't
necessarily real function starts) disassembles to two complete, clean
functions back to back -- itself (`0x98`, a small div-with-overflow-guard
helper ending in `jr $ra`/`addiu $sp,$sp,0x18`) and an unnamed
`func_800FC02C` (`0x2f0`, ending in its own `jr $ra`/`addiu $sp,$sp,0x30`)
-- `0x98 + 0x2f0 = 0x388`, matching the original declared size exactly,
with every internal branch in both halves staying inside its own range.
Fixed via the normal `syms.toml` split (`BattleTanxGASyms/
battletanxga.us.rev0.syms.toml`), no `manual_funcs`/N64Recomp source patch
needed this time. Verified: regenerated via the local `N64RecompCLI`,
confirmed both `player_text_1AE0` and `func_800FC02C` compile as separate
functions (`RecompiledFuncs/funcs_17.c:7476` and `:7586`), and a full
`ninja BattleTanxGARecompiled` build succeeded end-to-end. Not yet
confirmed against a real run.

## 2026-09-28, round 54: the scheduler deadlock is fully resolved -- new bug class, a "shared tail code" merged-function variant that needs a manual_funcs registration (and a required local N64Recomp source patch)

Round 53's second yield fix worked: the game got past the entire rounds
47-53 threading deadlock and crashed with a *new* error --
`Failed to find function at 0x8009F02C` (`librecomp/src/overlays.cpp:368`'s
`get_function()`, the runtime resolver for genuinely indirect calls) --
confirming real forward progress into new code.

This looked like the same merged-function-boundary bug as rounds 39-46 at
first, but it isn't. `0x8009F02C` sits mid-instruction-stream inside the
already-declared `func_8009EFD4` (it's literally the delay slot right after
`j 0x8009F048` at `0x8009F028`). Tried the normal fix (a `syms.toml` split)
and N64Recomp itself refused it outright: `func_8009EFD4`'s own internal
control flow branches to `0x8009F030` *and* jumps to `0x8009F048` -- two
different interior offsets of what the split would have carved out --
("branching outside of the function" / "Unhandled branch"). This is a
genuinely different bug shape: real shared/reused tail code, reachable both
as a normal fallthrough continuation of `func_8009EFD4` *and* as an
independent external entry point via an indirect call elsewhere in the ROM
-- not a case of the decompiler mis-drawing one function's boundary.

**The right tool turned out to already exist in N64Recomp**: `manual_funcs`,
a top-level config array (`N64Recomp/src/config.cpp`'s `get_manual_funcs`)
that registers an *additional*, independently-compiled function at a given
vram+size, coexisting with an already-declared overlapping function rather
than replacing it (`N64Recomp/src/main.cpp`'s `add_manual_functions`) --
exactly the "same bytes, reachable from two different entry addresses"
case here. Added it (`battletanxga.us.rev0.toml`'s `[input]` table):
```
manual_funcs = [
    { name = "func_8009F02C", section = ".resident_first_mb", vram = 0x8009f02c, size = 0x38 },
]
```
(size 0x38 chosen by disassembling forward from `0x8009F02C` to its own
clean `jr $ra` at `0x8009F05C` -- straight-line code, no branches leaving
that range, so it compiles as a fully self-contained unit on its own.)

**Caught by testing before telling the user, not by inspection**: adding
this to the TOML alone did nothing (`Function count` stayed at 1310,
identical with or without the entry) -- traced it to `add_manual_functions`
only being called from the *ELF-input* branch of `N64Recomp/src/main.cpp`
(guarded by `if (!config.elf_path.empty())`), never from the *ROM +
symbols-file* branch this project actually uses. Since N64RecompCLI is a
pure local codegen tool in this sandbox -- only the `RecompiledFuncs/*.c`
it generates gets committed/built, never the tool's own source -- patched
`N64Recomp/src/main.cpp` locally to also call `add_manual_functions` in the
ROM branch, rebuilt N64RecompCLI locally, regenerated, and confirmed
`Function count` went 1310 -> 1311 with `func_8009F02C` now present in
both `RecompiledFuncs/funcs_23.c` (a clean, self-contained translation
matching the hand-disassembled bytes exactly) and `recomp_overlays.inl`'s
lookup table. Full `ninja BattleTanxGARecompiled` build succeeded
end-to-end. Reverted the local `N64Recomp/src/main.cpp` patch afterward
(`RecompiledFuncs/` is gitignored -- nothing from this sandbox's own
regeneration gets committed either way).

**This local patch is required for the user's own regeneration too** --
their `N64Recomp.exe` is built from the same unmodified upstream submodule
source, so without this same source change, their build would silently
ignore `manual_funcs` exactly like this sandbox did on the first attempt,
and they'd hit the identical crash again. Gave them the equivalent
PowerShell script (same idempotent, marker-checked pattern used for the
RT64 submodule edits earlier in this project) to apply locally before
regenerating, since `lib/N64ModernRuntime` (and its nested `N64Recomp`) are
real git submodules whose edits can't be committed/pushed through this
project's normal workflow.

Not yet confirmed against a real run.

## 2026-09-28, round 53: found the real reason func_800A1290 never runs -- round 51's yield only covered one of func_800A1384's two loop-back paths

The round 52 (part 4) VI-dispatch diagnostic printed once
(`is_game_started=1`, `mq 0x80222930: validCount=0 msgCount=8`) and then
never printed again even after 15-20+ seconds of the game genuinely
running (confirmed debugger-free, confirmed CPU still pegged at ~10%
i.e. one core, so something was still actively spinning). That combination
-- still spinning, but our own diagnostic (inside
`btga_yield_via_priority_drop`) never firing again -- meant the active
code had stopped calling our yield helper entirely, despite still being in
`func_800A1384` (confirmed via a fresh debugger stack: still
`func_800A1384` -> `func_800BF80C` -> `func_8009D3A4` -> `func_8009EEA0`,
same as every round since 51).

Root cause: `func_800A1384` has a second loop-back path round 51 missed.
After the bounded 32-iteration inner copy (`L_800A139C`,
`RecompiledFuncs/funcs_8.c`) finishes one pass, a field check at vram
`0x800A13F8` (`sp+0x1F0 < 5`) can jump directly back to `L_800A139C`
*without* ever passing back through `L_800A1398` (the outer label round 51
put the only yield at) -- so once this specific branch starts getting
taken repeatedly, the function falls into a tight, entirely un-yielding
cycle that our round 51 fix never touches again. That's a "not unique to
this one spin site" case within a single already-patched function, not
a new function.

**Fix:** added a second `[[patches.hook]]` for `func_800A1384`, this time
at `before_vram = 0x800A139C` (the inner loop's own label) -- both loop-back
paths converge on this exact address, so there's no way to target only the
bypass path; the yield now also fires on every 16-byte inner-copy
iteration (up to 32x per outer pass) rather than just once. That's a real
per-pass slowdown on this one copy loop, but negligible next to hanging
indefinitely, and it stops firing at all once whatever this loop polls for
is finally satisfied. Verified: regenerated via the local `N64RecompCLI`,
confirmed both hooks land correctly at their respective labels
(`funcs_8.c:219-226`), and a full `ninja BattleTanxGARecompiled` build
succeeded end-to-end. Not yet confirmed against a real run.

## 2026-09-28, round 52 (part 4): the corrected diagnostic shows the queue is properly created; traced the full delivery chain and added a live VI-dispatch diagnostic

The corrected (sign-extension-fixed) diagnostic printed `msgCount=2,
msg=0x80217048` for mq `0x80217030` -- exactly matching `func_800985A0`'s
`osCreateMesgQueue(0x80217030, 0x80217048, count=2)` call
(`RecompiledFuncs/funcs_5.c:7717-7733`). So the queue is genuinely fine;
it's just never receiving anything. Traced the actual sender by searching
every `osSendMesg`/`osJamMesg` and `osSetEventMesg`/`osViSetEvent` call
site in the whole recompiled codebase (grepping for the literal offset
`0X7030` across all files, not just the known functions) rather than
assuming: found `func_80098AFC` (`funcs_5.c:8613+`, right next to
`func_80098B2C`, the flag-setter round 47 already identified) is the only
place that calls `osSendMesg(0x80217030, ...)` anywhere in the ROM. Its
only caller is `func_800A140C` (`RecompiledFuncs/funcs_8.c:300+`), whose
only caller is `func_800A1290` -- the audio dispatcher thread that has
shown up "correctly idle, blocked in osRecvMesg" in *every* debugger dump
since round 47. Reading `func_800A1290` in full (`funcs_8.c:18+`) shows
it's a message dispatcher: it blocks on `osRecvMesg` for mq `0x80222930`,
then jumps through a 5-entry table keyed on `(msg - 0x29A)`; case 0
(msg == `0x29A`, the VI event value registered via `func_800A1150`'s
`osViSetEvent` call, round 48) calls exactly `func_800A140C` ->
`func_80098AFC` -> sends to `0x80217030`.

So the full chain is: ultramodern's VI thread enqueues an external message
(`0x29A`) for mq `0x80222930` -> `func_800A1290` receives it and dispatches
-> `func_800A140C` -> `func_80098AFC` -> `func_800988E8` finally unblocks.
Round 52's drain-then-priority-drop fix should make this flow end-to-end,
but `func_800A1290` still hasn't moved in any dump taken since. Rather than
guess further, added a throttled (~1/sec) live diagnostic directly in
`src/main/scheduler_workaround.cpp` (a real C++ file with direct access to
`ultramodern::is_game_started()` and raw `rdram`, no sign-extension pitfall
this time -- used plain `vram - 0x80000000` unsigned arithmetic instead of
replicating `MEM_W`'s macro by hand) that prints `is_game_started()` plus
mq `0x80222930`'s live `validCount`/`msgCount` on every call to
`btga_yield_via_priority_drop`, throttled so it doesn't flood. This will
show empirically whether the VI thread is even sending anything yet, or
whether messages are arriving but something else is preventing
`func_800A1290` specifically from ever being scheduled to consume them.
Verified: full `ninja BattleTanxGARecompiled` build succeeded end-to-end.
Not yet confirmed against a real run.

## 2026-09-28, round 52 (part 3): the first diagnostic read was garbage -- fixed a sign-extension bug in my own patch, and traced the real creator function

The round 52 part 2 diagnostic printed all-zero fields for the queue at
`0x80217030`, which looked like confirmation it was never initialized --
but before trusting that, traced its actual creator to rule out a race:
`func_800985A0` (`RecompiledFuncs/funcs_5.c:7683+`) calls
`osCreateMesgQueue(0x80217010, ...)`, then `osCreateMesgQueue(0x80217030,
...)` (exactly this queue), then `osCreateThread`+`osStartThread` targeting
entry point `0x800988E8` -- i.e. `func_800988E8` itself. Its sole caller,
`func_8009D270` (`RecompiledFuncs/funcs_6.c:12551`), calls it unconditionally
and synchronously, strictly *before* the already-confirmed-working
`func_800A1150` call later in the same straight-line function -- so by
construction the queue must already be initialized before this thread's
own recv could ever run. That contradiction meant the diagnostic itself was
suspect, not the queue.

Found the bug: `MEM_W`'s address math (`recomp.h`) requires its address
operand to be a *sign-extended* 64-bit KSEG0 address (upper 32 bits all 1s) --
every real call site gets this by assigning through the `S32()` macro into a
64-bit `gpr`, which sign-extends automatically in C. The diagnostic instead
passed the bare literal `0x80217030` directly, which C types as a positive
32-bit `unsigned int` that never gets sign-extended, so `MEM_W`'s internal
`- 0xFFFFFFFF80000000` subtraction landed roughly 4GB off from the real
queue and silently read unrelated (zeroed) memory. The "never initialized"
finding was an artifact of my own patch, not a real result.

**Fix:** route the address constant through `S32()` into a `gpr` local
first (`battletanxga.us.rev0.toml`, same hook site), exactly matching how
the generated code builds every other address. Verified: regenerated,
confirmed the fixed version lands correctly
(`RecompiledFuncs/funcs_5.c:8244-8264`), passed `clang -fsyntax-only`
clean, and a full `ninja BattleTanxGARecompiled` build succeeded end-to-end.
Still purely diagnostic -- waiting on the corrected printout from a real
run before drawing any conclusion about why this thread is actually stuck.

## 2026-09-28, round 52 (part 2): traced func_800988E8's stuck queue to a missing osCreateViManager implementation; added a one-shot diagnostic print to confirm before writing a fix

After the drain-then-priority-drop fix (round 52 part 1), the game still
hangs -- confirmed via debugger this is genuine progress-then-restall, not
a no-op: the active thread now visibly cycles between `func_80098B40` and
`func_800A1384` (both caught mid-`btga_yield_via_priority_drop`, not bare
spinning), but `func_800988E8` -- the thread that needs to run to actually
clear things -- remains frozen at its very first `osRecvMesg` (mq vram
`0x80217030`), completely unmoved across all of rounds 47-52.

Traced this queue's likely registration path by reading `func_800A1150`
(`RecompiledFuncs/funcs_7.c:11098+`, the function round 48 already
identified as doing all of this thread's startup registration) in full:
it creates an `OSMesgQueue` at vram `0x80222930`, then calls
`osCreateViManager(0xFE)` -- and `osCreateViManager_recomp`
(`librecomp/src/vi.cpp:13-15`) is a complete no-op, same as
`osCreatePiManager_recomp` (`pi.cpp:60-62`). Real libultra's VI/PI Manager
threads exist specifically to fan a single hardware event out to *multiple*
application-registered queues, since raw `osSetEventMesg`/`osViSetEvent`
only support one global consumer each (confirmed by reading
`ultramodern/src/events.cpp` in full: `vi_thread_func` only ever sends to
`events_context.vi`/`.ai`'s single registered queue, nothing else). If this
game relies on the Manager pattern to route ticks to `0x80217030` (a
separate, per-thread queue, distinct from `0x80222930`), it would never
receive anything now that the Manager is a stub -- which exactly matches
what's observed. (Ruled out PI DMA completion as a factor: `osPiStartDma`/
`osEPiStartDma`, `pi.cpp:312-344`, deliver directly to a queue passed
per-call via `do_dma`, entirely independent of the broken
`osSetEventMesg(OS_EVENT_PI, ...)` path, so that part is *not* broken.)

Rather than keep guessing from static analysis, added a one-shot diagnostic
(`battletanxga.us.rev0.toml`, `[[patches.hook]]` on `func_800988E8` at its
own entry, guarded by a `static` so it only ever prints once) that dumps
the real `OSMesgQueue` struct fields at `0x80217030` (`blocked_on_recv`,
`blocked_on_send`, `validCount`, `first`, `msgCount`, `msg`) to stdout the
first time this function runs -- before it hits the blocking recv. This
will confirm empirically whether the queue was ever initialized
(`osCreateMesgQueue`, i.e. `msgCount` nonzero) and whether anything has
ever been sent to it (`validCount` nonzero at any point), rather than
continuing to infer from source alone. Verified: regenerated via the local
`N64RecompCLI`, confirmed the patch landed at the very top of
`func_800988E8` (`RecompiledFuncs/funcs_5.c:8244-8263`, before the register
save even completes), and a full `ninja BattleTanxGARecompiled` build
succeeded end-to-end. Purely diagnostic -- no behavior change yet, pending
the printed values from a real run.

## 2026-09-28, round 52: found why round 50/51's fix still hangs -- it never drains the external-message queue that VI/AI event delivery depends on

Round 51's fix built and ran, but the game still hung (same `AppHangB1`).
Debugger dump this round: the previously-fixed thread is now back at
`func_80098B40`, but genuinely cycling through the yield each iteration
(caught mid-`osSetThreadPri` inside `btga_yield_via_priority_drop`, not a
bare unyielding spin) -- so the mechanism runs, but the flag it waits on
(vram `0x80229230`) still never clears. All other threads unchanged from
every prior round, and critically: `func_800988E8` (the thread rounds
47-48 identified as needing to run to clear that flag) is still parked at
the *exact same* `osRecvMesg` call (`RecompiledFuncs/funcs_5.c` line 8301)
it has been at since round 47 -- meaning it has made zero progress across
five straight rounds, regardless of what round 50/51 changed.

Root cause: read `ultramodern/src/mesgqueue.cpp` in full. `osSendMesg`
skips `do_send` (the function that actually writes into a target's
`OSMesgQueue` and moves a `blocked_on_recv` thread into the ready queue) for
any non-game thread and instead calls `enqueue_external_message`
(`mesgqueue.cpp:34-36`), which just pushes onto a completely separate
`external_messages` concurrent queue. Ultramodern's VI/AI event thread
(`events.cpp`) is exactly such a non-game thread. That external queue is
only ever drained -- and the message actually delivered to the real target
queue -- by a game thread calling `wait_for_external_message`/
`_timed` (`mesgqueue.cpp:53-68`), which is what round 49's original
`yield_self_1ms` did but round 50/51's `btga_yield_via_priority_drop`
*doesn't*: it only calls `osGetThreadPri`/`osSetThreadPri`, which merely
rescans the already-ready `running_queue` via `check_running_queue` --
it never touches `external_messages` at all. So `func_800988E8`'s VI/AI
message was very likely sitting in that queue the entire time, and nothing
in rounds 50-51's fix ever pulled it out to actually deliver it.

**Fix (`src/main/scheduler_workaround.cpp`):** `btga_yield_via_priority_drop`
now calls `yield_self_1ms` first (drains `external_messages`, delivering
any pending event message and moving its receiver into the ready queue,
plus its own strict-priority `check_running_queue`), and only then does the
priority-drop trick -- so a thread that just became ready from a drained
message also gets an actual handoff despite not being strictly
higher-priority than the poller. Both steps are necessary: round 49 proved
draining alone isn't enough (strict priority check), and rounds 50-51 proved
the priority drop alone isn't enough (nothing to drain the external queue).
No TOML/generated-code changes needed this round, just the shared helper.
Verified: full `ninja BattleTanxGARecompiled` rebuild succeeded end-to-end
with no errors. Not yet confirmed against a real run.

## 2026-09-28, round 51: round 50 confirmed working via debugger; found and fixed a second, structurally identical scheduler gap

Round 50's priority-drop fix genuinely resolved the `func_80098B40` spin --
confirmed via a fresh debugger dump: that thread's call stack no longer
shows `func_80098B40` at all. The same two threads remain correctly idle
(`func_800A1290` and `func_800977DC`/`func_800FF698`, both blocked in
`osRecvMesg` on their own queues, unchanged from before), one thread is
still normally parked after a self-directed `osSetThreadPri`
(`func_8009EE08`, same as every prior round), and `func_800988E8`'s thread
is still sitting at the exact same `osRecvMesg` (line 8301) it was at
before round 50 -- meaning it genuinely hasn't progressed yet, consistent
with it still waiting on a VI/AI message rather than on the flag round 50
fixed.

The game still hangs (same `AppHangB1`), but the *active* thread moved to a
new call site: `func_800A1384` (`RecompiledFuncs/funcs_8.c:206`), called via
`func_800BF80C` -> `func_8009D3A4` -> `func_8009EEA0` (same overall thread
as before). This is a different code shape from `func_80098B40`'s plain
`while (flag != 0) {}`, but the same underlying bug: it copies a 0x208-byte
struct from a fixed shared address (vram `0x80222930` -- the same "plain
data accessor" pointer round 46 identified next to the audio DMA callback
thread `func_800A1290`) into a stack buffer, then re-loops back to the copy
start if fields inside the just-copied data (offsets `0x1F4`/`0x1FC`)
haven't reached an expected value -- i.e. polling another thread's write via
a copy-and-recheck pattern instead of a plain `while`, with no OS call
inside it, so it never gives the scheduler a chance either. Exactly the
"not unique to this one spin site" risk round 48 flagged.

**Fix:** factored round 50's priority-drop logic out of the inline
`[[patches.hook]]` text into a real shared helper
(`src/main/scheduler_workaround.cpp`, `btga_yield_via_priority_drop`, plain
`extern "C"`) instead of duplicating it per site, since this is now used in
two places and will likely be needed again. Both `func_80098B40` and the
new `func_800A1384` site (`battletanxga.us.rev0.toml`, `before_vram =
0x800A1398` -- the outer loop's own re-entry label, so it runs every full
copy pass but not inside the bounded 32-iteration inner copy) now just
forward-declare and call it. Verified: regenerated via the local
`N64RecompCLI`, confirmed both call sites landed correctly in the generated
source, and a full `cmake -S . -B build && ninja BattleTanxGARecompiled`
succeeded end-to-end -- including linking the final executable, confirming
the new helper resolves correctly against `osGetThreadPri`/`osSetThreadPri`
across translation units. Not yet confirmed against a real run (still
pending the user's machine).

## 2026-09-28, round 50: round 49 confirmed insufficient via debugger -- real fix: drop the spinning thread's own priority to force the swap

Round 49's `yield_self_1ms` injection did not resolve the hang (user report:
"same sort of result as before"). Confirmed why by attaching the debugger to
`build-dbg\` (the Release `build\` exe shows no Call Stack symbols -- a
recurring gotcha in this session, always use `build-dbg\` for debugging) and
walking every Game N thread's call stack while hung:

- Three threads are correctly idle, blocked in `wait_for_resumed`/
  `Semaphore::wait()` after a normal `osRecvMesg` (`func_800A1290`,
  `func_800988E8` -- the same thread round 47/48 identified as the one that
  needs to run to clear the `0x80229230` flag, still blocked on its VI/AI
  message queue) or after a self-directed `osSetThreadPri` call inside
  `func_8009EE08` (which *did* correctly trigger a real swap-and-park --
  confirming `osSetThreadPri`'s priority-change-triggers-`check_running_queue`
  path genuinely works).
- One thread's stack has `func_80098B40` directly at the top with nothing
  above it (`func_8009EEA0` -> `func_8009D3A4` -> `func_8009D270` ->
  `func_80098B40`) -- proof it's still spinning: it already returned from
  the injected `yield_self_1ms()` call and is back in the busy-wait, exactly
  the failure mode round 49's entry flagged as a risk.

Root cause confirmed by reading the runtime directly: `check_running_queue`
(`ultramodern/src/scheduling.cpp:24`) only swaps when
`next_thread->priority > self->priority` (strictly greater). Real libultra's
`osYieldThread` -- which round-robins to an *equal*-or-higher-priority ready
thread, not just a strictly-higher one -- is entirely unimplemented in this
runtime fork: `librecomp/src/ultra_translation.cpp:31-34`'s
`osYieldThread_recomp` just `assert(false)`s with the real call commented
out, and `ultramodern::osYieldThread` (declared in `ultra64.h:272`) has no
definition anywhere in the tree. So `yield_self_1ms` alone can never hand
off to a same-or-lower-priority thread, which is exactly the situation here.

**Fix (`battletanxga.us.rev0.toml`, same `[[patches.hook]]` site,
`before_vram = 0x80098B40`):** replaced the `yield_self_1ms()` call with a
temporary self-priority drop using only public, already-proven-working
functions (`osGetThreadPri`/`osSetThreadPri`, both plain `extern` forward
declarations, no runtime/submodule edit needed): save the current priority,
set it to 0 (`osSetThreadPri(rdram, 0, 0)` -- `t_ == 0` means "self" per
`threads.cpp:309-311`), which triggers `check_running_queue` via the
priority-actually-changed path (`threads.cpp:314-322`) and makes the strict
`>` check trivially true for virtually any other ready thread, then restore
the saved priority once control returns. Verified: regenerated via the
locally-built `N64RecompCLI` in this sandbox, confirmed the patch text lands
correctly right after the loop's label (`RecompiledFuncs/funcs_6.c:7-14`,
wrapped in `{ }` same as round 49 for the label/declaration C rule), passed
a clean `clang -fsyntax-only`, and built the real `RecompiledFuncs` ninja
target end-to-end with zero errors (only pre-existing unrelated warnings).

**Not yet confirmed against a real run** (this sandbox has no GPU/display).
If this still doesn't resolve it, the next things to check: whether
`func_800988E8`'s thread is even in `running_queue` yet at all when
`func_80098B40`'s thread yields (if it hasn't been inserted there -- e.g.
still blocked on the VI/AI message itself rather than ready -- no priority
trick helps, and the real question becomes why the VI thread's message
hasn't reached it), and whether `thread_queue_insert`'s priority-based
ordering could still starve the now-lowered-priority spinning thread longer
than expected once it's the one waiting to be resumed.

## 2026-09-28, round 49: attempted fix for round 48's deadlock -- inject a scheduler-yield call into the spin loop via [[patches.hook]]

Round 48 identified two real paths forward; this is an attempt at the
scoped one (option 2), avoiding a shared-runtime change. Ultramodern
already has exactly the primitive needed:
`yield_self_1ms` (`ultramodern/src/scheduling.cpp:45-48`, `extern "C"`,
not exposed in a public header) waits briefly for an external message, then
calls `check_running_queue` to switch to a higher-priority ready thread if
one exists -- precisely the "give the scheduler a chance" operation the
bare `while (flag != 0) {}` in `func_80098B40` never does.

Added a `[[patches.hook]]` entry (`battletanxga.us.rev0.toml`) at
`func_80098B40`'s `before_vram = 0x80098B40` (the loop's own re-entry
label, so it runs every iteration including the first) that calls it:
```
{ extern void yield_self_1ms(uint8_t *rdram); yield_self_1ms(rdram); }
```
Wrapped in a compound statement because a label in C can't be directly
followed by a declaration pre-C23 (first attempt without the braces
compiled only via a clang extension, `-Wc23-extensions`; confirmed clean
under a real `-fsyntax-only` check once wrapped). Verified the patch
applies and the generated code is both syntactically correct and
positioned exactly where intended by regenerating with a locally-built
`N64RecompCLI` in this cloud sandbox (this session has the ROM staged
locally too, same as previous rounds) -- something previous rounds always
had to defer to the user's machine for.

**Not yet confirmed against a real run.** If `yield_self_1ms` only
switches when a strictly higher-priority thread is ready
(`check_running_queue`'s condition, `scheduling.cpp:24`), and the thread
that needs to clear the flag isn't higher-priority than this one, this
specific fix won't be enough on its own -- worth checking thread
priorities if this doesn't resolve it.

## 2026-09-28, round 48: root cause of round 47's hang found -- a real hardware-interrupt-dependent busy-wait is incompatible with ultramodern's purely-cooperative thread scheduler

Continued from round 47. Two more pieces confirmed the actual mechanism:

**The VI/AI event registration round 47 wondered about does happen.**
Traced `func_800A1150`'s only caller: it's called from inside
`func_8009D270` (`RecompiledFuncs/funcs_6.c:12616-12619`), on the exact
same thread (entry point `func_8009EEA0`) that later reaches the
`func_80098B40` spin, and *before* it gets there. `func_800A1150` itself
(`RecompiledFuncs/funcs_7.c:11098+`) calls `osSetEventMesg_recomp` three
times (message values `0x29B`/`0x29C`/`0x29E`) and `osViSetEvent_recomp`
once (message value `0x29A`) -- exactly the message range
`func_800988E8`'s dispatcher checks for. So VI/AI event registration is
not the blocker; it already happened.

**The real blocker: `func_80098B40` is a plain busy-wait with no yield,
and ultramodern's N64-thread scheduler has no preemption.** Checked
`ultramodern/src/timer.cpp` (the "Timer Thread" seen in every thread
dump): it only exists to service game-requested `osSetTimer`/`osStopTimer`
timers, not to time-slice between N64 threads. Cross-referencing every
`resume_thread_and_wait`/`run_next_thread_and_wait`/`wait_for_resumed`
call site confirms the cooperative model is entirely voluntary: an N64
thread only ever hands off control at specific recognized library calls
(`osRecvMesg`, `osSetThreadPri`, etc.). `func_80098B40` is a raw
`while (*(int32_t*)0x80229230 != 0) {}` loop (`RecompiledFuncs/funcs_6.c`)
-- it calls nothing recognized, so once its host thread becomes "the
active N64 thread," no other N64 thread (including whichever one is
supposed to write `0` to that address and let this one continue) can ever
become active again. This also cleanly explains the steady ~10% CPU
reported: one thread pegged at 100% on its own core on a multi-core
machine, not intermittent scheduling -- a genuine hard wait, not a slow
one.

On real N64 hardware this same busy-wait pattern works because the
SI/controller-read completion is delivered by an actual hardware
interrupt, which preempts whatever's running unconditionally, regardless
of whether the interrupted code "cooperates." Ultramodern's software
model has no equivalent for this: nothing here preempts a thread that
doesn't voluntarily yield. This is very likely not unique to this one spin
site -- any similar `while (mem_flag) {}` polling pattern elsewhere in the
recompiled code would hit the same wall.

**Not yet resolved; two real options, neither of which is a quick fix:**
1. Add genuine preemption to ultramodern's thread scheduler (e.g. a
   periodic forced-yield check), which is a runtime-level change affecting
   every N64Recomp project built on this fork, not something scoped to
   this project alone.
2. Find exactly which thread is supposed to write `0` to vram `0x80229230`
   (search for the second write site beyond `func_80098B2C`'s `sw $v0` at
   `funcs_5.c:8636` -- the clearing code around `funcs_5.c:8412`,
   vram `0x80098A00`, is inside `func_800988E8`, itself currently blocked
   on its own `osRecvMesg`) and understand precisely why *that* thread
   hasn't run since the lock was set -- if it turns out to be reachable
   from a thread that isn't itself downstream of the spin, a targeted
   `[[patches.hook]]` TOML patch inserting a yield check inside the spin
   loop's address range could resolve this one call site without touching
   the shared runtime, but confirming that requires more thread-dependency
   tracing than done so far.

## 2026-09-28, round 47: game now runs real multi-threaded N64 logic -- hangs waiting for VI/AI event registration that hasn't happened yet

With round 46's fix in, the missing-function crashes stopped entirely.
Clicking "Play" now runs the game far enough to spawn multiple real N64
`osCreateThread` threads (seen in the debugger as "Game 1"/"Game 2"/
"Game 3" x2/"Game 5" -- the `t->id`-based naming from `get_game_thread_name`
in `main.cpp`) and hang instead of crash, with the whole process sitting at
a steady ~10% CPU.

Diagnosed by attaching the debugger, Break All, and walking every non-pool
thread's call stack (same technique used throughout this session):

- Two threads are legitimately idle, correctly blocked in `osRecvMesg`
  inside `func_800A1290` (round 46's split -- confirmed to be the audio
  driver's `__CallBackDmaNew`-adjacent worker thread, its own entry point)
  and `func_800988E8` (round 45's split), each waiting on their own message
  queue. Nothing wrong with these on their own -- they're supposed to sit
  idle until something sends them a message.
- One thread (entry point `func_8009EEA0`, round 39's split) is the one
  actually consuming CPU: it's spinning in a tight busy-wait loop,
  `func_80098B40` (`RecompiledFuncs/funcs_6.c`), on a flag at vram
  `0x80229230` (`while (*(int32_t*)0x80229230 != 0) {}`). That flag is set
  by `func_80098B2C` (called from 4 sites across the recompiled game code)
  and is supposed to be cleared after `osContGetReadData` (`vram
  0x80103254`, a real libultra function, correctly identified by name in
  `syms.toml`) completes -- i.e. this is a mutex protecting a synchronous
  controller-read, not something that should ever spin for long.

Traced the likely root cause one level further: `func_800988E8`'s own main
loop (`RecompiledFuncs/funcs_5.c:8244+`) starts by blocking on *two*
sequential `osRecvMesg` calls (queues at vram `0x80217030` and
`0x80217010`) before it ever reaches the controller-read/lock-clear code
further down -- and this thread is the one currently sitting in the first
of those two `osRecvMesg` calls. Those two queues are almost certainly the
game's own VI (frame tick) and AI (audio) event queues. Ultramodern's VI
thread (`ultramodern/src/events.cpp` `vi_thread_func`, lines 236-255) only
sends VI/AI messages once `ultramodern::is_game_started()` is true (which
it is, confirmed via `recomp::start_game()`/`game_status`) *and* the
target `OSMesgQueue` has actually been registered via `osSetEventMesg`/
`osViSetEvent` (`events.cpp:149-177`, both correctly implemented as
native functions here) -- so if the game's own startup code hasn't yet
called those to register its VI/AI queues, this thread will wait forever,
exactly matching what's observed.

**Not yet resolved.** The open question is *why* that registration hasn't
happened -- almost certainly because whichever game thread is supposed to
call `osSetEventMesg`/`osViSetEvent` hasn't been scheduled yet in the
cooperative (single-thread-active-at-a-time) emulated threading model,
possibly entangled with the controller-read spinlock above. This is
qualitatively different from every fix in rounds 39-46: those were
concrete, one-shot infrastructure/decompilation bugs found by following a
crash address to its exact cause; this needs mapping out this specific
game's own multi-thread startup order (which thread runs first, what each
one is blocked on, and why the registration thread either hasn't run or
silently failed) -- open-ended reverse engineering, not a quick fix.

Next steps for whoever picks this up:
1. Find where in the recompiled code `osSetEventMesg`/`osViSetEvent` are
   actually called from (search `RecompiledFuncs/*.c` for
   `osSetEventMesg_recomp`/`osViSetEvent_recomp` call sites), and trace
   backwards to find which thread/function is supposed to reach that call
   and why it hasn't yet.
2. Consider whether the controller-read spinlock (`func_80098B2C`/
   `func_80098B40`) is itself blocking that registration (e.g. if the
   registering code is gated behind the same lock, or scheduled after it).
3. Worth checking `ultramodern::run_next_thread_and_wait`/the cooperative
   scheduler's thread-priority ordering (several `osSetThreadPri` calls
   were seen in these same call stacks) in case a priority-ordering bug is
   preventing the right thread from ever getting scheduled.

## 2026-09-28, round 46: sixth confirmed merged-function boundary

Same bug class as rounds 39/41/42/44/45, found via the next runtime crash
address (`0x800A1290`) after round 45's fix. `__CallBackDmaNew` (declared
size `0xcc`) is really two functions: itself, a tiny 4-instruction
trampoline (`lui/addiu/jr/nop`, real size `0x10`) that returns some other
constant data pointer (`0x80222930`, not code -- just a plain accessor,
not a function-pointer-table entry like some of the earlier trampolines
found), and a separate jump-table dispatcher function right after it at
`0x800A1290` (real size `0xbc`, named `func_800A1290` -- no known original
symbol) ending exactly at the existing `func_800A134C` boundary.
`0x10 + 0xbc = 0xcc`, matching the original total exactly.

## 2026-09-28, round 45: fifth confirmed merged-function boundary

Same bug class as rounds 39/41/42/44, found via the next runtime crash
address (`0x800988E8`) after round 44's fix. `func_800985A0` (declared size
`0x55c`) disassembles to two complete functions back to back --
`func_800985A0` (`0x348`) and `func_800988E8` (`0x214`, the missing
address) -- ending exactly at the original declared boundary.
`0x348 + 0x214 = 0x55c`, matching the original total exactly.

## 2026-09-28, round 44: fourth confirmed merged-function boundary

Same bug class as rounds 39/41/42, found via the next runtime crash address
(`0x80097794`) after round 43's entrypoint sign-extension fix got past
`do_rom_read` and into real gameplay code. `func_800976AC` (declared size
`0x260`) disassembles to four complete functions back to back --
`func_800976AC` (`0xe8`), `func_80097794` (`0x48`, the missing address),
`func_800977DC` (`0x68`), and `func_80097844` (`0xc8`) -- ending exactly at
the original declared boundary with no leftover padding.
`0xe8+0x48+0x68+0xc8 = 0x260`, matching the original total exactly.

## 2026-09-28, round 43: entrypoint_address wasn't sign-extended -- crashed on the very first RDRAM write in do_rom_read(), immediately after the game actually started

With rounds 39-42 clearing every startup/render/lookup-table bug, clicking
"Play" after loading the ROM finally reached real game-boot code -- and hit
a new crash immediately: access violation inside `recomp::do_rom_read`
(`librecomp/src/pi.cpp:72`, `MEM_B(i, ram_address) = *rom_addr;`), on the
very first loop iteration (`i=0`). This call happens inside `init()`
(`recomp.cpp:494-502`), which runs once per game boot, well before
`recomp_entrypoint` (the actual recompiled game code) is ever reached --
so this is a distinct code path from anything exercised so far, not a
regression in previously-working code.

Locals at the crash: `ram_address` (the entrypoint address passed through
from `GameEntry::entrypoint_address`) showed as decimal `2147946496`, i.e.
hex `0x80071000` -- correct in *value*, but zero-extended
(`0x0000000080071000` as the actual 64-bit `gpr` bit pattern) rather than
sign-extended (`0xFFFFFFFF80071000`). `rom_addr` and `rdram` both looked
individually valid (non-null, plausible contents), which is what pointed
at the address *computation* rather than either raw pointer.

Root cause: `MEM_B`/`MEM_W`/`MEM_H` (`N64Recomp/include/recomp.h:95-108`)
compute `rdram + (((reg + offset) ^ N) - 0xFFFFFFFF80000000)` -- that
constant is the KSEG0 base (`0x80000000`) in its *sign-extended* 64-bit
form, matching real MIPS64: a 32-bit value loaded into a 64-bit register is
always sign-extended, so every register value these macros are normally
fed with with is already in `0xFFFFFFFF80xxxxxx` form. `entrypoint_address`
is declared `gpr` (`librecomp/include/librecomp/game.hpp:34`, a `uint64_t`
typedef) but this project's `main.cpp` set it from a plain
`0x80071000` literal, which the compiler zero-extends on implicit
conversion to `uint64_t` (`0x0000000080071000`), not sign-extends. Feeding
that zero-extended form into the macro's subtraction computes an offset
~4GB too large (`0x0000000080071000 - 0xFFFFFFFF80000000` wraps to
`0x0000000100071003` after the `^3` and subtraction, not the intended
`0x71000`), landing far outside the actual RDRAM allocation and segfaulting
on the very first byte write.

N64Recomp's own generator already produces the fix for this, just never
used: `RecompiledFuncs/lookup.cpp`'s auto-generated
`get_entrypoint_address()` returns `(gpr)(int32_t)0x80071000u` -- the cast
through `int32_t` (a signed type) before converting to `gpr` forces sign
extension. Confirmed this is exactly BanjoRecomp's own pattern too
(`src/main/main.cpp`: `.entrypoint_address = get_entrypoint_address()`).
This project's `main.cpp` never called that generated function at all,
just hardcoded the raw literal.

Fixed with the same cast applied inline in `main.cpp`'s
`supported_games` entry (`.entrypoint_address = (gpr)(int32_t)0x80071000u`)
rather than depending on the generated `get_entrypoint_address()` directly,
since that function only exists once `RecompiledFuncs/` has real content --
depending on it directly would break the project's existing
"builds fine without the ROM yet" placeholder path (same reasoning as the
`BTGA_HAS_RECOMPILED_FUNCS` guard added in round 40 for
`register_overlays.cpp`).

**Not yet confirmed against a real run.**

## 2026-09-28, round 42: third confirmed merged-function boundary

Same bug class as rounds 39/41, found via the next runtime crash address
(`0x800FF698`) after round 41's fix. `func_800FF560` (declared size
`0x2c0`) disassembles to two complete functions back to back --
`func_800FF560` (real size `0x138`) and `func_800FF698` (real size
`0x17c`) -- followed by `0xc` bytes of zero-word padding already correctly
excluded by the original boundary (the next declared function,
`func_800FF820`, already started at the right place). `0x138 + 0x17c + 0xc
= 0x2c0`, matching the original total exactly.

## 2026-09-28, round 41: second confirmed merged-function boundary, found once register_overlays() actually started working -- and a systematic scan attempt that didn't pan out

Round 40's `register_overlays()` fix worked -- confirmed by a completely
different crash address (`0x800FF1A4`, not `0x8009EE08`) on the very next
run, meaning the lookup table is genuinely live now. Same bug class as
round 39: `func_800FF0D4` (declared size `0x148`) disassembles to three
functions back to back -- `func_800FF0D4` (real size `0xd0`), a tiny
3-instruction trampoline at `func_800FF1A4` (size `0xc`, just
`lui $v0, HI / jr $ra / addiu $v0, $v0, LO` -- computes a constant address
into `$v0`, exactly the shape of a jump-table/function-pointer-table
entry, consistent with being reached only by an indirect call) and
`func_800FF1B0` (size `0x6c`), matching the original total exactly
(`0xd0 + 0xc + 0x6c = 0x148`).

Tried writing a systematic scanner (`scan_merged_funcs.py`, not checked in)
to find more of these across all 1304 declared functions at once rather
than one crash at a time: flag any function containing a `jr $ra` +
delay-slot pair followed by what looks like another function's own
prologue (`addiu $sp, $sp, -N`) before the declared end. Produced 313
candidates out of 1304 functions -- far too high a rate to be trustworthy;
almost certainly mostly false positives from inline jump-table data whose
raw words coincidentally decode as a matching instruction pattern. Both
confirmed splits so far were found by following the actual runtime crash
address as ground truth, not by static heuristic guessing, so abandoned
the batch approach and went back to fixing these reactively as the user
hits them -- slower per-instance but far more reliable, and the process
itself (disassemble the declared range, find the real `jr $ra`/prologue
boundaries, split the `syms.toml` entry, regenerate, rebuild) is now fast
and well-practiced.

## 2026-09-28, round 40: round 39's fix didn't actually take -- recomp::overlays::register_overlays() was never called at all, so the function lookup table was never wired up in the first place

Round 39's `syms.toml` split was correct (confirmed: regenerating produced
`func_8009EE08` with the right offset/size in
`RecompiledFuncs/recomp_overlays.inl`), but the exact same
`"Failed to find function at 0x8009EE08"` error persisted anyway, even
against a freshly-deleted-and-rebuilt exe. Root cause was one level up:
`recomp::overlays::register_overlays()` (`librecomp/src/overlays.cpp`) --
which populates the `func_map` that `get_function()` searches -- was never
called anywhere in this project at all. Confirmed by grepping the entire
repo for the call and finding only the declaration/definition in the
library itself.

N64Recomp generates `RecompiledFuncs/recomp_overlays.inl` (a `static
SectionTableEntry section_table[]` plus per-section function/reloc arrays)
on every run, but that file is meant to be `#include`d and wired up by the
*game project's own code* -- N64Recomp itself never does this, and this
project never had that glue. It mostly didn't matter: N64Recomp resolves
direct `jal` calls into direct C function calls at recompile time, so only
genuinely *indirect* calls (function pointers, jump tables) ever needed the
runtime `func_map` lookup at all -- and this is apparently the first (or
one of very few) indirect calls the game makes, which is why everything up
to this point ran fine despite the lookup table being permanently empty.

Confirmed via `BanjoRecomp` (github.com/BanjoRecomp/BanjoRecomp, same
toolchain) that this glue is expected to be hand-written per-project: its
`src/main/register_overlays.cpp` `#include`s its own generated
`recomp_overlays.inl` and calls `register_overlays()` with the resulting
`section_table`/`num_sections`/`overlay_sections_by_index` symbols, called
from `main()` before `recomp::start()`.

Added the same pattern here: `src/main/register_overlays.cpp` (declares
`void register_btga_overlays()`, `#include`s
`../../RecompiledFuncs/recomp_overlays.inl`, calls
`recomp::overlays::register_overlays(...)`), called from `main()` before
anything else. Since `recomp_overlays.inl` doesn't exist on a fresh clone
before the ROM/N64Recomp step (same as the rest of `RecompiledFuncs/`),
gated the real implementation behind a new `BTGA_HAS_RECOMPILED_FUNCS`
compile definition (`CMakeLists.txt`, set when
`RecompiledFuncs/recomp_overlays.inl` exists at configure time) with a
no-op fallback, so the existing "builds fine without the ROM yet" placeholder
path still works.

Also found and fixed, same debugging session: the "RT64 Idle" GPU
power-throttling-prevention thread
(`lib/rt64/src/hle/rt64_workload_queue.cpp:1179`, `WorkloadQueue::
idleThreadLoop`) crashed with an access violation deep inside
`amdxc64.dll`'s driver code specifically on the user's AMD Radeon RX 5700 XT
(RDNA1) -- not covered by RT64's existing AMD driver workaround table in
`rt64_application.cpp`, which only handles RDNA3/RDNA4-era cards. It's
explicitly an optional feature (its own comment: "not required if the
driver is configured to be at the Max Performance power state"), so
disabled it via a one-line local edit to `set_application_user_config`
(`RecompFrontend/recompui/src/renderer/rt64_render_context.cpp`,
`application->userConfig.idleWorkActive = false;`) rather than debugging
the driver crash itself. This is a submodule edit and can't be pushed
through this repo's normal git flow (`RecompFrontend` is a real git
submodule pointing at N64Recomp's own upstream repo) -- it needs to be
reapplied locally after any fresh clone/submodule reset until a better
place for it is found (a project-level patch-on-configure step, or
upstreaming a fix to RT64's own AMD workaround table).

**Not yet confirmed fixed against a real run** -- both changes need a
rebuild and test on the user's machine.

## 2026-09-28, round 39: first real function-boundary bug found and fixed -- func_8009ED9C was actually three separate functions merged into one

With rounds 37-38's fixes in, the game finally boots, opens a responsive
window, loads the user's ROM, and starts executing real recompiled N64 code
-- reaching, for the first time in this project's history, an honest
reverse-engineering-content crash instead of an infrastructure one:
`"Failed to find function at 0x8009EE08"` (`librecomp/src/overlays.cpp:364-372`,
`get_function`'s deliberate `assert(false); std::exit(EXIT_FAILURE);` path
for an indirect call/jump target with no matching entry in `func_map`).

`0x8009EE08` isn't a gap between declared functions -- it falls *inside*
the declared range of `func_8009ED9C` (`BattleTanxGASyms/battletanxga.us.rev0.syms.toml`,
vram `0x8009ed9c`, declared size `0x13c`, i.e. `0x8009ed9c`-`0x8009eed8`).
Disassembled the whole range by hand with `tools/mini_mips_disasm.py`
(rom offset = vram - `0x80070000`, per `tools/splat.yaml`'s documented
mapping) against the ROM staged locally in this sandbox
(`BattleTanx Global Assault (USA).z64`, gitignored, never committed).

The disassembly shows three complete, independent functions back to back,
not one function with internal control flow:
- `0x8009ed9c`-`0x8009ee04`: standard prologue (`addiu $sp,$sp,-0x28` /
  `sw $ra,...`) through its own `jr $ra`/`nop` epilogue at `0x8009ee00`/
  `0x8009ee04`. Real size `0x6c`.
- `0x8009ee08`-`0x8009ee9c`: another standalone prologue immediately after,
  own epilogue at `0x8009ee98`/`0x8009ee9c`. Real size `0x98`. This is the
  one the game calls indirectly and N64Recomp couldn't resolve, since only
  `0x8009ed9c` was a declared function entry.
- `0x8009eea0`-`0x8009eed4`: third standalone prologue/epilogue pair. Real
  size `0x38`.
- `0x8009eed8`-`0x8009eee0`: 8 bytes of non-code (decodes as garbage/data,
  e.g. `.word 0x4D504149`), matching the existing `func_8009EEE0` entry's
  own start -- this padding was already correctly excluded by the original
  boundary, which is why the total (`0x6c + 0x98 + 0x38 = 0x13c`) matches
  the original declared size exactly. Nothing was gained or lost; the
  original pass just found the right *outer* boundary and missed the two
  real splits inside it.

This is a concrete instance of the "sizes are still mostly gap-derived...
rather than checked one-by-one" caveat `PROGRESS.md` item 3 already flagged
as an open risk. Fixed by splitting the one `syms.toml` entry into three
(`func_8009ED9C` size `0x6c`, `func_8009EE08` size `0x98`, `func_8009EEA0`
size `0x38`).

**Not yet regenerated/rebuilt or confirmed fixed** -- this requires
re-running `N64RecompCLI` (`BUILDING.md` step 4) to regenerate
`RecompiledFuncs/*.c` from the corrected symbols, a CMake reconfigure
(`file(GLOB ...)` is configure-time-only, per earlier rounds), and a
rebuild, all on the user's Windows machine. There are almost certainly more
of these elsewhere in the 1288-entry symbol table -- this was one found by
following the exact runtime crash address, not a systematic sweep.

## 2026-09-28, round 38: window opened but was permanently "(Not Responding)" -- update_gfx was reading input state, not pumping SDL/Win32 events

With round 37's crash fixed, the game window opened for the first time ever
but Windows immediately marked it "(Not Responding)" and it never rendered
anything, despite the process clearly still running (non-zero, moving CPU
usage; nothing hung at the OS level).

Root cause: `update_gfx` (`src/main/main.cpp`, the
`ultramodern::gfx_callbacks_t::update_gfx` callback, invoked every
iteration of `recomp::start`'s main loop) was calling
`recompinput::poll_inputs()`. That function
(`RecompFrontend/recompinput/src/input_state.cpp:34`) only reads
*already-buffered* SDL state (`SDL_GetKeyboardState`, controller state,
mouse deltas) -- it never calls `SDL_PollEvent`, so the window's Win32
message queue was never being serviced at all, which is exactly what makes
Windows mark a window unresponsive regardless of whether the app is
otherwise looping fine.

Found by comparing against `BanjoRecomp` (github.com/BanjoRecomp/BanjoRecomp,
same author as N64Recomp itself, same RecompFrontend/N64ModernRuntime/RT64
stack) at the user's suggestion -- its own `update_gfx` calls
`recompinput::handle_events()` instead, which does call `SDL_PollEvent` in
a loop (`RecompFrontend/recompinput/src/input_events.cpp:249-253`).
`poll_inputs()` is already correctly wired elsewhere as
`ultramodern::input::callbacks_t::poll_input` and doesn't need to also run
from `update_gfx`. Fixed by switching `update_gfx` to call
`handle_events()` instead, matching BanjoRecomp exactly. Confirmed fixed:
the window became interactive immediately after (mouse hover/highlight and
clicks started working, including opening a native file-picker dialog for
ROM selection).

## 2026-09-28, round 37: the actual root cause of the whole-startup crash -- missing recompui::config::finalize() call, found by diffing against BanjoRecomp

Round 36 ended with the theory that a clean `Release` rebuild had fixed the
"cannot use 'throw' with exceptions disabled" build failures; it hadn't
resolved the real symptom, which turned out to be a separate runtime crash
entirely, and took a long debugging session (checkpoint-instrumented
startup logging in `main.cpp`, a `std::set_terminate` handler to surface
otherwise-lost exception messages, and eventually live debugger sessions
under `devenv /debugexe` with `RelWithDebInfo`/`Debug` builds) to actually
pin down.

The exe reliably crashed inside `RT64::Application::setup()` ->
`UIState::create_menus()` -> `recompui::config::init_modal()`
(`RecompFrontend/recompui/src/config/ui_config.cpp:138-145`), which throws
`"Config modal has already been initialized."` if `config_modal` (a
file-scope static, legitimately assignable only once, from inside this
exact function) is already non-null. Breakpoint-and-count confirmed
`init_modal()` is entered exactly once before the crash -- `config_modal`
was already non-null on its *first* call, which is inconsistent with any
normal double-invocation theory. In `Release` builds this same underlying
issue surfaced as a raw, uncatchable `0xC0000409` fail-fast in
`ucrtbase.dll` instead of a catchable C++ exception (same root cause,
different manifestation depending on build-specific memory/heap layout,
which is what made this so hard to pin down purely from stack traces and
crash offsets -- the `ucrtbase.dll` offset that kept recurring across many
unrelated bugs today turned out to just be the CRT's generic internal
abort-family entry point, not a fingerprint of one specific issue).

Root cause, found by cloning and reading `BanjoRecomp`
(github.com/BanjoRecomp/BanjoRecomp) at the user's suggestion -- another
project on this exact same toolchain. Its `main()`
(`src/main/main.cpp:745`) always calls `banjo::init_config()`
(`src/game/config.cpp:248-277`) before `recomp::start()`, which creates
several config tabs and then calls `recompui::config::finalize()`. This
project's `main.cpp` never called `finalize()` (or created any config
tabs) at all. `finalize()`'s own doc comment
(`RecompFrontend/recompui/include/recompui/config.h:90,129-131`) says it
"loads the config from disk" and "must be called after all tabs have been
created" -- skipping it left the config system in a state `init_modal()`
didn't expect.

Fixed by adding the missing call in `src/main/main.cpp`, before
`recomp::start()`: `recompui::config::create_general_tab()`,
`create_graphics_tab()`, `create_controls_tab()`, `create_sound_tab()`,
`create_mods_tab()` (the library's own prefab tabs, no game-specific
options exist yet per `PROGRESS.md` item 8), then `finalize()`. Confirmed
fixed: the startup crash is gone and the game reaches `create_render_context`
successfully.

## 2026-09-28, round 36: round 35's placeholder was too empty -- comment-only RCSS parses as failure, not success, crashing the same unguarded dereference from the other side

The user hit a `RelWithDebInfo` build regression while chasing a symbolized
stack trace for round 35's fix (several files -- rabbitizer, rmlui_debugger,
N64Recomp/cgenerator.cpp -- failed with "cannot use 'throw' with exceptions
disabled" under that build type specifically; not investigated further,
since it's an unrelated build-type quirk, not the actual bug). Went back to
`Release`, which doesn't have this problem, and later did a full clean
`build/` wipe to rule out object-file inconsistency from having switched
build types mid-stream on the same build directory.

With that clean Release build, the crash after "Loaded font face
'LatoLatin'..." changed from round 35's `std::length_error` to a genuine
access violation (`0xC0000005`, reading address `0x8` -- a null-pointer-plus-
small-offset pattern). Root cause, one call further into the same code path:
`Rml::Factory::InstanceStyleSheetStream` (`.../RmlUi/Source/Core/Factory.cpp:572-580`)
returns `nullptr` when parsing fails, and `init_styling`
(`ui_context.cpp:249`) dereferences that return value directly --
`MergeStyleSheetContainer(*Rml::Factory::InstanceStyleSheetStream(...))` --
with no null check. Same missing-defensive-check bug class as round 35's
`resize(tellg())` issue, in the very next line of the same function.

Why parsing failed: round 35's placeholder `assets/recomp.rcss` was
comment-only, and RmlUi's `StyleSheetParser::Parse`
(`StyleSheetParser.cpp:758`) returns `!style_sheets.empty()` -- a stylesheet
that never contained an actual rule block (only a `/* ... */` comment)
produces an empty `style_sheets` list, which counts as a parse *failure*,
not an empty-but-valid success. A truly empty or comment-only RCSS file is
not a safe placeholder here, contrary to what round 35 assumed.

Fixed by adding one trivial real rule (`body {}`) to `assets/recomp.rcss`,
which is enough for the parser to close out a non-empty stylesheet and
return `true`, avoiding the null dereference. Not yet confirmed against an
actual run.

## 2026-09-28, round 35: same story, next asset -- missing recomp.rcss crashes with std::length_error via an unguarded resize(tellg())

Round 34's font fix got past the `std::runtime_error`, but immediately hit
a new, different unhandled exception -- `std::length_error` -- right after
`UIState`'s constructor finished (confirmed by the console output: the
crash now comes right after "Loaded font face 'LatoLatin'..."). Visual
Studio's debugger never actually produced a symbolized call stack for this
one despite multiple attempts at a `RelWithDebInfo` rebuild (kept resolving
to "Module was built without symbols" for `BattleTanxGARecompiled.exe`, and
even a build that visibly reconfigured with symbols didn't get devenv to
load them) -- not worth chasing further, since reading the source directly
from the last known-good point (right after `UIState`'s constructor) found
the real bug first anyway.

`create_menus()` (called immediately after `UIState` is constructed, per
`ui_state.cpp`) starts with
`recompui::init_styling(recompui::file::get_asset_path("recomp.rcss"))`.
`init_styling` (`lib/RecompFrontend/recompui/src/core/ui_context.cpp:238-241`):

```cpp
std::ifstream style_stream{rcss_file};
style_stream.seekg(0, std::ios::end);
style.resize(style_stream.tellg());
style_stream.seekg(0, std::ios::beg);
```

`assets/recomp.rcss` doesn't exist in this project either (same missing-
asset class as round 34's fonts). `std::ifstream` doesn't throw on a
missing file by default -- it just opens in a failed state. `seekg` on a
failed stream is a no-op, and `tellg()` on a failed stream returns
`streampos(-1)` per the standard. That `-1` gets passed straight into
`std::string::resize()`, which takes an unsigned `size_t` -- so `-1`
becomes `SIZE_MAX`, and `resize(SIZE_MAX)` throws exactly
`std::length_error`. This is a real, reproducible bug in recompui's own
`init_styling` (no existence check, no `std::ifstream::failbit` handling),
not anything specific to this project or to Windows -- it would hit any
recompui-based project that doesn't ship this exact file. Not patching the
vendored submodule for it, consistent with this project's existing
practice (e.g. the cstdint/RmlUi PCH fix in `CMakeLists.txt` was done from
this project's side rather than editing the submodule) -- the missing
input is what's actually ours to fix.

Fixed the same way as round 34: supplied the missing asset rather than
patching the library. Added `assets/recomp.rcss` as an empty placeholder
(with a comment explaining why it exists and pointing back here) -- an
empty stylesheet is valid RCSS and merges cleanly with recompui's own base
styling via `MergeStyleSheetContainer`, so this doesn't silently break
anything, it just means this game has no styling on top of recompui's
defaults yet (unsurprising, since no UI assets have been extracted from
the ROM at all).

Not yet confirmed against an actual run. If this clears the crash, the
next real signal is still the same one round 34 was waiting on: does an
actual launcher menu render and respond to input.

## 2026-09-28, round 34: found the black-window crash -- the long-flagged missing-font gap, hit for the first time now that a real display exists

Got a real crash location via Visual Studio's debugger (`devenv.exe
/debugexe build\BattleTanxGARecompiled.exe`), since the exe links
`/SUBSYSTEM:WINDOWS` and produces no visible console output at all even
when run from a terminal: an unhandled `std::runtime_error`, thrown about a
second after launch, right after two "Failed to load font face ...
could not open file" log lines for `assets\NotoEmoji-Regular.ttf` and
`assets\promptfont/promptfont.ttf`.

Traced to `lib/RecompFrontend/recompui/src/base/ui_state.cpp:263-265`:
`UIState`'s constructor throws `std::runtime_error("No primary font was
registered with recompui::register_primary_font")` if
`recompui::register_primary_font(...)` was never called before
`recomp::start()`. It never was -- `src/main/main.cpp` has had this
exact line commented out with a TODO since round 23, and the file-level
comment has said so explicitly the whole time ("No font is registered...
The launcher menu will very likely be visually broken until then"). This
isn't a Windows bug, a regression, or anything round 25-33's toolchain
fixes touched -- `UIState`'s constructor runs as part of window/renderer
creation, which is the exact point every single earlier run in this
session (the display-less cloud sandbox) already failed at for unrelated
reasons (no GPU). This is genuinely the first time this line has ever
executed, on any platform, in this project's history -- round 33's black
window *was* the missing-font gap turning from "visually broken" (the old,
too-optimistic prediction) into "hard crash" the moment there was finally
a real window for it to matter in.

Fixed as a bootstrap placeholder, not final game UI work: copied
`LatoLatin-Regular.ttf` and `NotoEmoji-Regular.ttf` (both already vendored
under `lib/RecompFrontend/recompui/lib/RmlUi/Samples/assets/`, SIL Open
Font License 1.1) into this project's own `assets/` directory, alongside
their license text (`assets/FONT_LICENSE.txt`, required by OFL's
attribution terms), and uncommented/filled in the
`recompui::register_primary_font("LatoLatin-Regular.ttf", "Lato")` call
`main.cpp` already had a slot for. `assets/promptfont/promptfont.ttf` (a
controller-button icon font) is still missing and will still log its
"could not open file" warning -- that one was always tolerated gracefully
(only the *primary* font is a hard requirement) and isn't part of this fix.

Not yet confirmed against an actual run -- this needs only a rebuild (no
CMake reconfigure: `main.cpp` is already a tracked source, and asset files
aren't part of any glob), not a full pipeline redo. If this clears the
crash, the real next signal is whether a launcher menu actually renders
and is interactive -- the first opportunity in this project's history to
find out.

## 2026-09-28, round 33: first successful Windows build and run, ever -- BattleTanxGARecompiled.exe launches, shows a black window instead of a crash

After round 32's `N64RecompCLI` target fix and one more full reconfigure
(needed because the `build/` directory that finally linked successfully had
been configured *before* `RecompiledFuncs/` was regenerated with real
content -- CMake's `file(GLOB ...)` is configure-time-only, so the stale
empty-placeholder result was still in effect until a fresh `cmake -S . -B
build` re-ran the glob against the now-populated directory), the user got a
real `BattleTanxGARecompiled.exe`, ran it, and it launched: a window opened
instead of crashing or failing to start. This is the very first time this
project has run on an actual display anywhere -- everything through round
32 was validated only up to "fails cleanly at window/renderer creation" in
this session's own display-less cloud sandbox.

The window shows solid black rather than a launcher menu or the game.
Investigating next: whether this is the known RSP microcode gap
(PROGRESS.md item 6 -- `get_rsp_microcode` in `src/main/main.cpp` returns
`nullptr` unconditionally, so nothing this ROM's own display lists reference
is recognized, though RT64 ships generic F3D-family GBI fallback walkers
that may or may not cover this game without it) versus something more basic
failing in the recompui/RmlUi launcher UI itself (which should render
independently of any in-game RSP work). Asked the user whether the black
window persists (renderer running, nothing drawn) or exits/crashes shortly
after, and whether any console/diagnostic text is visible anywhere, before
narrowing further -- `BattleTanxGARecompiled.exe` links with
`/SUBSYSTEM:WINDOWS` (see the round-27-33 link commands), so this may be a
genuinely silent failure with no stderr visible at all regardless of cause,
which would itself need addressing before this is debuggable further from
the outside.

## 2026-09-28, round 32: real BUILDING.md bug, unrelated to Windows -- step 4 was building the wrong CMake target this whole time

With the x64 shell finally sorted (round 31), the Windows build got all the
way to the final `BattleTanxGARecompiled.exe` link and failed on exactly
one missing symbol: `recomp_entrypoint`. Traced it to `RecompiledFuncs/`
being empty on the user's machine -- this repo's own `CMakeLists.txt` has a
deliberate placeholder fallback for that (`file(GLOB ...)` finds nothing,
so it silently substitutes an empty `.c` file so the rest of the project
still configures on a fresh clone). That meant step 4 -- actually running
`N64Recomp.exe battletanxga.us.rev0.toml` to generate the recompiled game
code -- had never completed on this machine.

Tracing that down surfaced a real, standing bug in `BUILDING.md` itself,
present on **both** platforms, not a Windows-specific issue: step 4's
`cmake --build ... --target N64Recomp` builds the wrong CMake target.
`lib/N64ModernRuntime/N64Recomp/CMakeLists.txt` defines the actual CLI tool
as `add_executable(N64RecompCLI)` (line 117), which links against a
separate static library also confusingly named `N64Recomp`
(`add_library(N64Recomp ...)` elsewhere in that file -- also visible
directly in the main project's own final link command, as
`N64Recomp.lib` alongside `LiveRecomp.lib`/`SymbolLists.lib`). The CLI
executable gets its user-facing filename via
`set_target_properties(N64RecompCLI PROPERTIES OUTPUT_NAME N64Recomp)`
(line 129) -- so the *file* is correctly named `N64Recomp`/`N64Recomp.exe`,
but the *CMake target* you have to ask Ninja to build is `N64RecompCLI`.
Building `--target N64Recomp` instead silently builds only the static
library and stops -- no error, just the wrong, much smaller output, which
is exactly what the user's pasted log showed ("Linking CXX static library
N64Recomp.lib", 49/49, done -- no `.exe` anywhere).

This was wrong in `BUILDING.md` since it was first written (round 24-ish),
on both the Linux/macOS and Windows command blocks -- it likely went
unnoticed on Linux specifically in this session because earlier rounds'
Linux verification never re-ran step 4 from a truly fresh clone/build
directory after `BUILDING.md` was written; whatever `N64Recomp` binary was
used for those checks was almost certainly built earlier, before this
target-naming detail mattered, by whatever ad hoc command produced it at
the time. Fixed both command blocks in `BUILDING.md` to
`--target N64RecompCLI`, with a note explaining the executable's output
filename is still `N64Recomp.exe`/`N64Recomp` so the later "run it" step
doesn't need to change.

Not yet confirmed against an actual Windows build -- this fix should let
step 4 finally produce a real `N64Recomp.exe`, regenerate `RecompiledFuncs/`
for real, and get the final link past the `recomp_entrypoint` symbol. Next
real signal is whether `BattleTanxGARecompiled.exe` links successfully with
the real recompiled code in it.

## 2026-09-28, round 31: found it -- the dev shell itself was initialized for x86, not x64

Asked the user to check `$env:LIB` and confirm the base MSVC v143 build
tools component was installed (it was). The `LIB` answer was the whole
story:

```
...\VC\Tools\MSVC\14.44.35207\ATLMFC\lib\x86;...\VC\Tools\MSVC\14.44.35207\lib\x86;
...\Windows Kits\10\lib\10.0.26100.0\ucrt\x86;...\Windows Kits\10\lib\10.0.26100.0\um\x86
```

Every single entry ends in `\x86`, not `\x64`. The shell this whole session
had been troubleshooting in was never actually an x64 dev environment,
regardless of which shortcut or prompt name the user believed they'd
opened. That single fact explains the `mainCRTStartup` failure completely
and rules out every theory from rounds 27-30: `clang-cl` correctly resolved
to the x64-hosted binary (round 28's fix), correctly compiled and asked
`lld-link` for a `/machine:x64` link (visible in every failing log), and
`-MDd` correctly requested the debug CRT -- but `LIB` only listed the
**x86** copies of `msvcrtd.lib`/`vcruntimed.lib`/etc, so lld-link's
`/DEFAULTLIB:`-driven search for the x64 CRT import libraries found
nothing byte-compatible with a `/machine:x64` object and silently
contributed nothing, rather than erroring with an explicit architecture
mismatch. Every prior fix in this saga (rounds 25-29) was real and correct
for the bug it targeted, but none of them could have fixed this, because
none of them touched which dev-environment script had run.

No repo change for this one -- it's purely about which Start Menu shortcut
gets launched. Added a `BUILDING.md` sanity-check step (verify `$env:LIB`/
`%LIB%` contains `\x64` segments, not `\x86`, before doing anything else)
so a future reader hits this fast instead of chasing it through five
misleading compiler/linker errors the way this session did. If the user's
`build\` directory picks up a working x64 dev shell from here, the next
real signal is whether `BattleTanxGARecompiled` actually finishes linking.

## 2026-09-28, round 30: round 29's revert changed nothing -- this isn't a repo bug, it's the user's local toolchain/environment

Pulled round 29's revert, reconfigured. Identical failure, and this time
the compile command shows `--target=` is genuinely gone
(`clang-cl.exe  /nologo   /DWIN32 /D_WINDOWS  /Zi /Ob0 /Od /RTC1 -MDd ...`,
no target triple at all) -- yet `lld-link: error: <root>: undefined symbol:
mainCRTStartup` reproduces byte-for-byte identically. That rules out every
`CMakeLists.txt` change made in rounds 27-29: none of them were ever the
actual cause. This failure is happening entirely inside CMake's own
minimal one-.c-file "Check for working C compiler" self-test, which uses
none of this repository's own compile flags, include paths, or link
settings -- it's generated fresh by CMake itself every time, using only
`/DWIN32 /D_WINDOWS /Zi /Ob0 /Od /RTC1 -MDd` (CMake's fixed default ABI-
check flags) against a trivial `int main()`. There is nothing left in this
repo's build configuration that could be causing this.

Working theory, not yet confirmed: this is a local Visual Studio
installation/environment problem, not a code or CMakeLists.txt problem.
Two candidates flagged for the user to check directly (this session has no
Windows machine to check them on):
1. Whether `LIB` is actually populated in that shell (`$env:LIB` in
   PowerShell / `%LIB%` in cmd) -- if it's empty or missing the VC Tools
   MSVC lib directory specifically, the CRT import libraries that supply
   `mainCRTStartup` (`msvcrtd.lib`, `vcruntimed.lib`, etc.) wouldn't
   resolve even though they're normally pulled in automatically via a
   `/DEFAULTLIB:` directive clang-cl embeds for `-MDd`, rather than being
   named explicitly on the link line (the actually-named libs in the
   failing command -- kernel32.lib, user32.lib, etc. -- come from the
   Windows SDK, a separate component, and those aren't failing).
2. Whether the base "MSVC v143 - VS 2022 C++ x64/x86 build tools"
   component (not just "C++ Clang Compiler for Windows") is actually
   installed -- clang-cl doesn't ship its own copies of the CRT import
   libraries; it relies on the co-installed MSVC toolset's
   `VC\Tools\MSVC\<version>\lib\x64\` for those. If that base component
   didn't get installed alongside the Clang one, the referenced default
   libs may not exist on disk anywhere, causing exactly this failure
   pattern with no "cannot open file" diagnostic (since the reference
   itself may not even be getting embedded/considered, vs. embedded-but-
   unresolvable -- both would look similar from the outside without
   further isolation, e.g. a bare clang-cl+lld-link repro outside CMake).

## 2026-09-28, round 29: round 27's --target forcing was itself the bug -- reverted now that round 28's exact-path pin is the real fix

The user pulled round 28's fix (explicit full path to the x64-hosted
`clang-cl.exe`) and reconfigured. CMake's "Check for working C compiler"
step now correctly resolved and invoked
`.../VC/Tools/Llvm/x64/bin/clang-cl.exe` (confirmed in the log) -- and
still hit the *exact same* failure as round 28:

```
lld-link: error: <root>: undefined symbol: mainCRTStartup
```

This is the tell: round 27's `CMAKE_C_COMPILER_TARGET`/
`CMAKE_CXX_COMPILER_TARGET` forcing (`x86_64-pc-windows-msvc`) was never
actually necessary, and is the thing causing this failure. Before round 27
existed, this exact CMake self-test (and everything after it) passed
without any explicit `--target=` flag -- rounds 25/26's real failures were
never about this trivial test-compile step at all, only about which
physical clang-cl got invoked for the *project's own* source files later
in the build. Once round 28 pinned the exact x64-hosted binary by full
path, that binary's own natural default target is already
`x86_64-pc-windows-msvc` -- forcing the same value explicitly, redundantly,
via `CMAKE_C_COMPILER_TARGET` apparently changes clang-cl's internal
toolchain-selection path enough to break its implicit embedding of the CRT
default-library directive (the `/DEFAULTLIB:` COFF directive derived from
`-MD`/`-MDd`/etc that normally tells the linker which import library
supplies `mainCRTStartup`) -- reproduced identically on CMake's own
minimal one-file test program, with zero project-specific code involved,
so this isn't specific to anything in this repo's own sources.

Reverted round 27's `CMakeLists.txt` change entirely. The real, sufficient
fix is round 28's: pin `CMAKE_C_COMPILER`/`CMAKE_CXX_COMPILER` to the exact
`Tools\Llvm\x64\bin\clang-cl.exe` path (already in `BUILDING.md`). No
`--target=` forcing needed once the right binary is the one actually being
invoked -- it already knows its own architecture.

Not yet confirmed against an actual Windows build -- same standing caveat
as rounds 25-28. If `mainCRTStartup` still comes up undefined on the next
attempt even without the reverted flag, the next thing to check is whether
`LIB` (the environment variable naming where the CRT/Windows SDK import
libraries live, normally set by the VS dev shell launcher itself) is
actually populated in that shell -- `echo $env:LIB` in PowerShell or
`echo %LIB%` in cmd should show several semicolon-separated paths; if it's
empty, the dev environment wasn't actually initialized for that window
despite its name/shortcut.

## 2026-09-28, round 28: round 27's --target fix wasn't enough on its own -- the wrong clang-cl binary needs to be avoided, not compensated for

After deleting `build/` and reconfiguring fresh (picking up round 27's
`CMAKE_C_COMPILER_TARGET`/`CMAKE_CXX_COMPILER_TARGET` forcing), CMake's own
"Check for working C compiler" step got further -- the test object file
compiled fine this time with `--target=x86_64-pc-windows-msvc` visibly in
the command line -- but then failed at **link**:

```
lld-link: error: <root>: undefined symbol: mainCRTStartup
```

The compiler CMake resolved for bare `-DCMAKE_C_COMPILER=clang-cl` was, once
again, `VC\Tools\Llvm\bin\clang-cl.exe` (the 32-bit-hosted copy), not
`VC\Tools\Llvm\x64\bin\clang-cl.exe`. Forcing the target triple (round 27)
was necessary but not sufficient: it fixed the actual code generation
(hence the successful compile), but apparently that 32-bit-hosted binary's
default-CRT-library selection (the `/DEFAULTLIB:` directive normally
embedded into the object file based on the `/MDd`/`/MD` flag, which is what
tells the linker which CRT startup object provides `mainCRTStartup`) either
isn't emitted correctly, or isn't the x64 variant, when that particular
binary is forced to cross-target x64 via `--target=`. Whatever the exact
mechanism, compensating for the wrong binary via flags is fragile; the real
fix is to not invoke that binary at all.

Changed course: instead of relying on PATH to resolve bare `clang-cl` (which
has now picked the wrong one on this exact machine at least twice, in two
different shell sessions, despite both allegedly being launched from an
"x64 Native Tools Command Prompt for VS 2022"), `BUILDING.md`'s Windows
section now has the user set `CMAKE_C_COMPILER`/`CMAKE_CXX_COMPILER` to the
literal full path of `VC\Tools\Llvm\x64\bin\clang-cl.exe`, removing the
ambiguity entirely rather than trying to out-flag it. Left round 27's
`CMAKE_C_COMPILER_TARGET`/`CMAKE_CXX_COMPILER_TARGET` forcing in
`CMakeLists.txt` in place too, as a harmless (no-op when the right binary is
already used) second line of defense.

Not yet confirmed against an actual Windows build -- same standing caveat
as rounds 25-27. If explicitly pointing at the x64-hosted binary doesn't
clear this on the next attempt, the next thing to check is whether `LIB`
(the environment variable that tells `link.exe`/`lld-link.exe` where the
CRT/Windows SDK import libraries live) is actually set in that shell --
`vcvarsall.bat`/the Native Tools shortcut should set it, but if the user's
shortcut or script is stale or was edited, it might not be.

## 2026-09-28, round 27: round 26's fix worked too -- next failure was a silent 32-bit build, not a code bug

The user's next attempt got past `RecompiledFuncs` and reached `librecomp`
(`config_option.cpp`, `eep.cpp`, `config.cpp`), failing with:

```
recomp.h(62): error: use of undeclared identifier '_mul128'
recomp.h(66): error: use of undeclared identifier '_umul128'
mods.hpp(54): error: static assertion failed due to requirement 'sizeof(unsigned int) == 8'
    static_assert(sizeof(std::size_t) == 8);
```

Both are symptoms of one root cause, not two separate bugs: the build was
compiling for **32-bit x86, not 64-bit x64**. `_mul128`/`_umul128` are
128-bit-multiply intrinsics that only exist for x64 targets -- on x86 they
are genuinely undeclared, by design, not a missing include. `size_t` being
4 bytes only happens on a 32-bit target. Corroborating evidence: the
compiler path in this failing log was
`...\VC\Tools\Llvm\bin\clang-cl.exe` (no `\x64\`), whereas round 25/26's
working steps used `...\VC\Tools\Llvm\x64\bin\clang-cl.exe`. Visual Studio
ships both a 32-bit-hosted and a 64-bit-hosted clang-cl.exe under
`VC\Tools\Llvm\`; which one wins on PATH -- and what target architecture it
defaults to without an explicit `--target=`/`-m64` -- isn't fully pinned
down by running in an "x64 Native Tools Command Prompt" alone, at least not
reliably enough to trust across a whole multi-hundred-target build.

Fixed at the CMake level rather than by telling the user to pass yet more
manual flags: `CMakeLists.txt` now sets `CMAKE_C_COMPILER_TARGET` /
`CMAKE_CXX_COMPILER_TARGET` to `x86_64-pc-windows-msvc` on `WIN32`, **before**
the `project()` call. This has to be before `project()` -- that's where
CMake runs its own compiler ABI detection (`CMAKE_SIZEOF_VOID_P`), and this
repo's own `CMakeLists.txt` already branches on
`CMAKE_SIZEOF_VOID_P EQUAL 8` in a couple of places (the `-march=nehalem
-fms-extensions` block, and the Linux/Vulkan compile-definitions block) --
setting the target only via `CMAKE_CXX_FLAGS` after `project()` would have
fixed the actual compiled output's architecture while leaving CMake's own
internal ABI bookkeeping (and anything gated on it) still thinking it's a
32-bit build. Explicitly forcing the target this way is a no-op when the
correct x64 clang-cl was already being used, so it's safe either way.

This is a CMake **cache** issue too, not just a source fix: the user's
existing `build/` directory has stale (wrong-architecture) ABI detection
results baked into its `CMakeCache.txt`, generated before this fix existed.
Told the user to delete `build/` entirely and reconfigure from scratch
rather than relying on ninja's automatic incremental reconfigure, since
that wouldn't redo compiler ABI detection on its own.

Not yet confirmed against an actual Windows build -- same standing caveat
as rounds 25-26.

## 2026-09-28, round 26: round 25's fix worked -- and surfaced the same clang-cl `-include` bug a second time, on `RecompiledFuncs`

After round 25's `/FI cstdint` fix, the user's next Windows build attempt got
past `rmlui_core`'s PCH entirely (confirming that fix) and reached a new,
different failure at `RecompiledFuncs`'s own placeholder source:

```
clang-cl: warning: unknown argument ignored in clang-cl: '-include'
clang-cl: error: cannot specify '/Fo...' when compiling multiple source files
```

Same root cause as round 25, different symptom. `CMakeLists.txt`'s
`target_compile_options(RecompiledFuncs ...)` force-includes
`include/btga_recomp_hooks.h` (declarations for `[[patches.hook]]` entries)
via bare GNU-style `-include <path>` passed as two separate command-line
tokens. clang-cl doesn't recognize that two-token form at all -- it emits a
non-fatal "unknown argument ignored" warning for the bare `-include` token,
then treats the now-orphaned path token (`.../btga_recomp_hooks.h`) as a
second **input source file** rather than part of an include flag, which
makes it a genuine second source file on the command line alongside
`recompiled_funcs_placeholder.c` -- and clang-cl refuses to combine an
explicit `/Fo<output>` with multiple inputs. Round 25's case looked
different (a missing-file error, not a multiple-inputs error) only because
that flag was a single SHELL-quoted token (`-include cstdint`) rather than
two separate tokens; the underlying incompatibility (clang-cl not
understanding GNU `-include` at all) is the same.

Fixed the same way as round 25: `/FI<path>` (single token, no space needed
since a full path has no spaces to worry about here) when `MSVC` is set,
keeping `-include <path>` on GCC/Clang. Also proactively applied the same
fix to the other, currently-dormant `-include` usage in `CMakeLists.txt`
(the `rsp_stock_compat.hpp` force-include on `rsp/battletanx_audio.cpp` and
`rsp/f3dex.cpp` -- guarded by `if(EXISTS .../rsp/battletanx_audio.cpp)`,
which doesn't exist yet per PROGRESS.md item 6, so it wasn't hit in this
build, but it's the identical pattern and would have hit the identical bug
the moment RSP microcode work starts).

Not yet confirmed against an actual Windows build -- same caveat as round
25, this session has no Windows machine, so this is reasoned from the exact
same evidence pattern (a working `/FI` flag for the PCH header sitting right
next to a broken `-include` flag in round 25's failing command) rather than
verified directly. Next Windows build attempt is the real test.

## 2026-09-28, round 25: first real Windows build attempt -- found and fixed a clang-cl/PCH bug in our own `-include cstdint` workaround

The user attempted the actual Windows build for the first time (this project
had only ever been built on Linux before this). Two Windows-specific bugs
found earlier by code review alone (missing `SDL_SysWMinfo`-based HWND path
in `src/main/main.cpp`, unconditional reference to the nonexistent
`icons/app.rc`) did NOT surface as build errors, i.e. those fixes held.

A new, real error did surface, at the very first C++ file to build:
`rmlui_core`'s CMake-generated `cmake_pch.cxx`, with clang-cl reporting
`error: no such file or directory: 'cstdint'`. Traced to our own
`CMakeLists.txt` fix (added earlier this project, for a *different*,
Linux-side problem: upstream RmlUi 6.0 uses `uint32_t`/`uint8_t` etc.
without including `<cstdint>`, which a current libstdc++ rejects under
Clang) -- `target_compile_options(... "-include cstdint")` on
`rmlui_core`/`rmlui_debugger`/`recompui`.

That GNU-style `-include <bare-name>` flag apparently doesn't reliably
resolve a standard-library header by name under clang-cl specifically while
it's *also* generating a precompiled header (`/Yc`) in the same invocation --
even though the same command's `/FI` flag (MSVC-native forced-include,
already used by CMake's own PCH machinery to force-include `cmake_pch.hxx`
in that exact command) presumably works fine, and even though plain
`#include <...>` resolution elsewhere in the same build (e.g. `lunasvg`'s
`svgelement.cpp`, which uses `std::unique_ptr`) was never a problem. The two
other `-include` usages in this repo's own `CMakeLists.txt` (the
`btga_recomp_hooks.h` force-include on `RecompiledFuncs`, and the
`rsp_stock_compat.hpp` force-include for stock RSP microcode source files)
were not touched -- both pass a full absolute path rather than a bare
standard-library header name, so they don't route through the same
name-lookup path and aren't expected to hit this.

Fix: made the force-include compiler-aware. When `MSVC` is true (CMake sets
this for clang-cl too, via its MSVC-compatible frontend variant, not just
real `cl.exe`), use `/FI cstdint` instead of `-include cstdint`; keep the
GNU spelling for GCC/Clang on Linux, where it was already working.

This was found and fixed from the user's pasted build log alone -- no
Windows machine available in this session either, so the fix is reasoned
from the evidence (the failing flag, and the working `/FI` flag right next
to it in the same failing command) rather than confirmed by a green
Windows build yet. If the next attempt gets further, that's the confirmation;
if `/FI cstdint` itself turns out not to work the same way, the fallback is
to drop precompiled headers for these three targets on Windows entirely
(`set(RMLUI_PRECOMPILED_HEADERS OFF)` before `add_subdirectory(RecompFrontend)`)
rather than fighting the force-include mechanism further.

## 2026-09-28, round 24: wrote the 12 stock-runtime compat shims -- BattleTanxGARecompiled links, launches, and fails exactly where this sandbox's missing GPU says it should

Wrote `src/game/stock_runtime_compat.cpp` (COP0 status read, the 4 thread-
scheduler internals, SI access-queue creation, timer/VI internals) and
`src/game/controller_pak.cpp` (the 7 Controller Pak filesystem internals),
filling every symbol round 23 found missing. `bdragoncore/battle-tanx-
recomp`'s own files of the same name/path are the reason these exact paths
were already anticipated in `CMakeLists.txt`'s `BTGA_FORKED_RUNTIME` check --
same reasoning as round 23, only the generic shape carries over, not any of
that project's own values or logic.

None of this could be derived from public documentation alone with
confidence, so each function's real argument registers were checked
directly against how *this ROM's own code* actually calls it (found via the
same rabbitizer-based disassembly used throughout rounds 21-22):

- `__osGetSR_recomp`: returns 0, matching the established pattern for every
  other COP0 access in this ROM (already nopped via instruction patches) --
  nothing anywhere emulates real COP0 state.
- `__osEnqueueThread`/`__osDequeueThread` confirmed to take `(queue, thread)`
  in `$a0`/`$a1` at their real call sites (e.g. `jal __osEnqueueThread` at
  `0x80110614` with `$a0 = lw 0x8($t6)`, `$a1 = move $a1, $t6`) --
  implemented as thin wrappers around `ultramodern::thread_queue_insert`/
  `_remove`/`_pop`, which already do the same guest-memory OSThread-queue
  manipulation for the exported `osStartThread`/etc. `__osDispatchThread`
  maps to `ultramodern::run_next_thread_and_wait` (real hardware does this
  with a raw register/COP0 context switch; ultramodern does it with host
  threads + semaphores instead, so this is a translation of intent, not a
  literal port).
- `__osSiCreateAccessQueue`: real behavior read directly from this ROM's
  own bytes at its address (never recompiled since N64Recomp ignores it,
  but the raw bytes are still sitting right there in the ROM to
  disassemble) -- it's textbook libultra: `osCreateMesgQueue(&0x803B04F8,
  &0x803B04F0, 1)` then `osSendMesg(&0x803B04F8, NULL, 0)`, both already-
  real (not stubbed) exported functions in this ROM's own trusted symbol
  table. Replicated verbatim against the same two addresses so anything
  that later blocks on that queue doesn't deadlock waiting for a message
  that would otherwise never be posted.
- `__osTimerInterrupt`/`__osViSwapContext`: both called with zero arguments
  from this ROM's VI manager thread, matching their real void-void
  prototypes -- made no-ops, since ultramodern already manages both VI
  swaps and timer expiry through its own separate mechanism and duplicating
  that bookkeeping risked the two fighting each other. Flagged as the least
  certain of the six stock_runtime_compat.cpp shims -- revisit first if VI
  timing or timer-driven gameplay ever misbehaves once this is testable.
- The 7 Controller Pak functions: `src/main/main.cpp` already reports no
  pak connected on any port, so none of these need real protocol/storage
  behavior -- they return a consistent non-zero failure (`__osContAddressCrc`
  aside, which is pure arithmetic with no I/O and gets a real implementation
  from general familiarity with the public N64 pak-addressing CRC, flagged
  as not independently checked against a primary source). Revisit if real
  Controller Pak support (rumble or pak saves) is ever wanted.

**Result**: `BattleTanxGARecompiled` links (18.8MB, up from round 22's 15.8KB
placeholder -- confirms all 1300 recompiled functions are now actually
referenced and included) and, when run in this sandbox, correctly falls back
to "no audio device" (no ALSA card here) and then exits cleanly with
`Failed to create window: Vulkan support is either not configured in SDL or
not available` -- exactly the right failure, in exactly the right place,
for a container with no GPU/display. This confirms the boot path (SDL init
-> audio fallback -> window/renderer creation -> `recomp::start`) all runs
correctly up to the point real graphics hardware is required. Testing
further (does the launcher menu appear, does `recomp_entrypoint` run, does
the game boot) needs a machine with a real display and the actual ROM,
which this cloud sandbox is not.

## 2026-09-28, round 23: wrote the real entry point -- and hit exactly the wall PROGRESS.md item 7 predicted

Wrote `src/main/main.cpp`, the piece round 22 flagged as missing (nothing
called `recomp_entrypoint`, so the linker dropped all the recompiled game
code as unreferenced). Structurally modeled on
`bdragoncore/battle-tanx-recomp`'s own `src/main/main.cpp` (cloned to
`/home/user/bdragoncore/battle-tanx-recomp` for reference, same as before --
only the generic ultramodern/librecomp/recompui plumbing carries over, none
of BattleTanx's own game logic, addresses, or polish like its audio
resampling bridge or launcher theming, which this file deliberately leaves
out for now):

- Registers one `recomp::GameEntry` for this ROM: real entry point
  (`0x80071000`), and a real `rom_hash` -- **not** the N64 header CRC1/CRC2,
  but `XXH3_64` of the whole normalized big-endian `.z64` (computed directly
  against the ROM this project has been developed against:
  `0x9c7467e763553529`, `pip install xxhash`), since that's what
  `librecomp/src/recomp.cpp`'s `check_hash` actually compares against.
- `save_type` is `SaveType::AllowAll` -- this ROM's real save type
  (EEPROM/SRAM/FlashRAM) has never been determined; that's real
  undone work, not a considered choice.
- `get_rsp_microcode` returns `nullptr` unconditionally. RT64 ships its own
  generic F3DEX-family GBI interpreters (`lib/rt64/src/gbi/*.cpp` -- F3D,
  F3DEX, F3DEX2, F3DGolden, F3DPD, F3DWave, F3DZEX2, L3DEX2, S2DEX, S2DEX2,
  Extended -- all already built successfully as part of round 22's RT64
  build) that handle GFX tasks via HLE without needing this game's own
  recompiled microcode, so graphics may work without any RSP work at all.
  Audio tasks have no such fallback -- the game will hit
  `quick_exit` printing the unhandled task type the first time it submits
  an `M_AUDTASK`, which is expected until PROGRESS.md item 6 happens.
- No font is registered (`register_primary_font` call is commented out) --
  there's no font file under `assets/` yet, so RmlUi has nothing to render
  UI text with. The launcher menu will likely be visually broken (invisible
  or fallback-glyph text) until one is added.
- Audio playback is a plain `SDL_QueueAudio` push with no resampling --
  functional enough to tell whether audio comes out at all, not tuned to
  sound clean.
- Discovered by trying to actually link it: `recompui`'s own code declares
  `extern SDL_Window* window;` (`ui_state.cpp`) and `default_launcher_init_
  callback` (`ui_launcher.cpp`) reads a global `std::vector<recomp::
  GameEntry> supported_games` **by that exact name** -- these aren't
  optional customization points, they're required extern symbols any game
  project using this frontend must define. Neither is documented anywhere
  outside the source itself; found both only from the linker's undefined-
  reference output naming them.
- Fixed a real `CMakeLists.txt` bug this surfaced: `recompui` and
  `recompinput` (and, separately, `RecompiledFuncs` and `librecomp`/
  `ultramodern`) reference each other's/each layer's symbols without CMake
  knowing about the cross-target cycle, so a single left-to-right static-
  archive scan left resolvable symbols (`recompui::controls_page`) undefined
  depending on which object a given `.a` happened to pull in first on its
  one pass. Wrapped the whole `target_link_libraries(BattleTanxGARecompiled
  ...)` list in `-Wl,--start-group`/`--end-group`. Learned the hard way that
  this has to be literal arguments *inside* the same `target_link_libraries`
  call -- two separate `target_link_options` calls (one for
  `--start-group`, one for `--end-group`) do NOT interleave with the
  library list in call order; CMake collects link options and link
  libraries into separate property lists and concatenates them at fixed
  positions in the final command regardless of when each was called, so
  both flags landed adjacent to each other before the library list instead
  of wrapping it.
- Corrected `patches/recompui_event_structs.h` (added in round 22) to
  exactly match `lib/RecompFrontend/recompui/include/recompui/
  event_structs.h` -- an actual reference copy sitting in that include
  directory for exactly this purpose that a plain grep for the filename
  `ui_types.h`'s own comment names ("must be kept in sync with
  patches/recompui_event_structs.h") had missed on the first pass. My
  hand-derived version used different enum names/types (`int32_t` instead
  of `bool`, `RECOMPUI_EVENT_TEXT` instead of `UI_EVENT_RESERVED1`, etc.) --
  functionally equivalent, but there was no reason to diverge from the
  library's own canonical copy once it was found.

**Where it stands now**: the link fails on exactly 12 undefined
`*_recomp` symbols, all C functions our recompiled code calls that N64Recomp's
built-in `reimplemented_funcs` list expects some runtime to provide, and
neither `librecomp` nor `ultramodern` do (confirmed by grepping their
entire source for each name -- zero matches, not a link-order problem this
time). This is precisely PROGRESS.md item 7,
"stock-runtime compatibility shims" -- the original BattleTanx needed its
own hand-written `stock_runtime_compat.cpp`/`controller_pak.cpp` for the
same reason, and it's now confirmed (not just suspected) that Global
Assault needs its own equivalent too. The 12 symbols split into four real
subsystems, none implemented yet:
  - `__osGetSR_recomp` -- COP0 Status register read.
  - `__osDequeueThread_recomp`, `__osDispatchThread_recomp`,
    `__osPopThread_recomp`, `__osEnqueueThread_recomp` -- libultra thread
    scheduler internals (called from this ROM's own `osDestroyThread`/
    interrupt-handler code, not just from the exception-vector dead code
    already stubbed in `battletanxga.us.rev0.toml`).
  - `__osContAddressCrc_recomp`, `__osPfsSelectBank_recomp`,
    `__osContRamWrite_recomp`, `__osContRamRead_recomp`,
    `__osCheckPackId_recomp`, `__osPfsRWInode_recomp`,
    `__osRepairPackId_recomp` -- Controller Pak (memory card) filesystem
    internals, exactly the `controller_pak.cpp`-shaped gap the CMakeLists.txt
    comment already flagged as unknown.
  - `__osSiCreateAccessQueue_recomp` -- SI (controller port) access queue
    setup.
  - `__osTimerInterrupt_recomp`, `__osViSwapContext_recomp` -- timer
    interrupt and VI (video interface) context-swap internals.

Next step: write real implementations for these 12 functions (a genuine
new round of work, not a quick patch -- four distinct subsystems, most
needing to be understood from libultra's real behavior rather than this
ROM's own disassembly, since these are OS-layer internals the game calls
into rather than game logic).

## 2026-09-27, round 22: first full build -- `BattleTanxGARecompiled` links and runs

Checked out the submodules `.gitmodules` had listed but that had never
actually been added (`git submodule add` for N64ModernRuntime,
RecompFrontend, rt64, plus all of rt64's own ~16 nested submodules and
N64ModernRuntime's/RecompFrontend's own few), installed the missing system
deps (`libvulkan-dev`, `libsdl2-dev`, `libgtk-3-dev`), and iterated the full
CMake + ninja build against round 21's clean N64Recomp output until it
linked. `./build/BattleTanxGARecompiled` now exists and runs (exit 0 --
it's still the placeholder `main()`, see below for what that means).

Two kinds of work this round:

**More symbol-table bugs, found because N64Recomp not erroring during its
own analysis pass doesn't mean the C it generates actually compiles.**
Round 21 declared victory at `N64Recomp`'s exit code 0; round 22 found 9
more function-boundary problems that only surfaced once `gcc` tried to
compile `RecompiledFuncs/*.c`:
- A branch/jump leaving its function is only turned into a proper tail
  call when the target is the exact START of some known function
  (`recompilation.cpp`'s `functions_by_vram.find`); otherwise N64Recomp
  emits `goto L_<addr>` to a label that's never defined, and `gcc` fails
  with "label ... used but not defined". Found and fixed 9 of these by
  splitting the target function at that exact address, including one
  chain reaction (splitting `func_800DB1B0` revealed a *second* branch
  into what became the new `func_800DB258`, needing a further split) --
  wrote a small script that repeats the branch-target scan and
  auto-splits until none remain, rather than fixing them one rebuild at a
  time.
- A write to `$zero` (other than the literal canonical nop encoding)
  compiles to invalid C (`0 = ...;`), since N64Recomp's codegen doesn't
  special-case it. `rabbitizer`'s `outputsToGprZero()`/`isNop()` catch
  this class directly -- found `and $zero,$zero,$zero`, `mfhi $zero`,
  `sllv/srlv $zero,...` and more, all the same trailing-garbage-word
  pattern as round 21's fixes, plus five more fully-fake ~0x10-0x20-byte
  entries in the same persistently bad 0x8011axxx neighborhood.
- The hardest class: a garbage word that decodes as a **fully plausible,
  syntactically valid instruction with real-looking operands** --
  `beql $s2, $s5, ...`/`bne $t3, $t6, ...` built from what's actually
  mid-string ASCII bytes. Nothing in a static per-instruction check flags
  these; the only tell is where their *computed branch target* lands.
  Found 3 this way (`func_800E5BB8`, `func_800F7EC0`, and
  `func_800F8660` -- the last one had actually been logging as an
  "Indirect tail call" during N64Recomp's own analysis with no warning at
  all, then still broke the C compile; **N64Recomp's own log output not
  complaining about a function is not proof it will compile**). Also
  found two ENTIRE functions this way that round 21 had wrongly kept as
  real code (`func_8014749C`, `func_80147740` -- every single instruction
  in both was this same repeating-word garbage, `0x52945294`, matching
  round 16's already-known texture/palette data pattern; deleted both).
- Wrote a proactive full-corpus sweep for "a branch/jump target that is
  neither inside its own function nor the start of any known function" to
  catch the rest of this category in one pass instead of one rebuild
  error at a time -- converged to 0 remaining after applying its findings
  (except the one deliberately-left-alone jump-table case in
  `func_800F8660`, later fixed for real once the C compiler caught it too).
- `n_alEnvmixerPull`'s own stub (round 21) needed to extend to a second
  function, `func_801000B0`, split out of it by the branch-target sweep --
  it contains the same unanalyzable jump table, so it's stubbed for the
  same reason.

**Missing pieces in the actual CMake/build wiring**, none of which had
ever been exercised against a real build before:
- `include/btga_recomp_hooks.h` (force-included into every
  `RecompiledFuncs/*.c`, per `CMakeLists.txt`) didn't exist. Everything
  the current hooks need (`ctx`, `S32`/`S64`/`U32`/`U64`) already comes
  from N64Recomp's own generated `recomp.h`, so this is a placeholder for
  now -- add real declarations here as future hooks need them.
- The `patches/*.c` -> `patches.elf` -> `N64RecompCLI patches.toml` ->
  `RecompiledPatches/patches.c` pipeline (`PatchesLib` in
  `CMakeLists.txt`) assumed real patch sources and a `patches.toml` that
  don't exist yet (PROGRESS.md item 8 is explicitly not started).
  Guarded it the same way `RecompiledFuncs`/`BattleTanxGARecompiled`
  already guard their own not-yet-written sources: build `PatchesLib` as
  an empty placeholder until real patches exist, instead of failing
  outright (`ld.lld: error: no input files`).
- `lib/RecompFrontend/recompui/src/api/ui_api_events.cpp` unconditionally
  `#include`s `patches/ui_funcs.h` (marked `// TODO: Forced game
  includes`) for a `RecompuiEventData` struct/enum set that's meant to be
  generated per-game. Wrote `patches/recompui_event_structs.h` by hand
  from the actual field usage in that file plus the enum values in
  `lib/RecompFrontend/recompui/src/elements/ui_types.h` (which even names
  the expected filename in a comment -- "must be kept in sync with
  patches/recompui_event_structs.h"), and had `ui_funcs.h` include it.
  This is real, needed-now content (unlike the hooks placeholder above),
  not a stub -- but still has no actual game callback declarations in it
  yet, since there are no UI-driving patches to declare.

**What "runs" means right now**: `BattleTanxGARecompiled` links
successfully with all 1300 recompiled functions in `RecompiledFuncs`, all
of RT64/N64ModernRuntime/RecompFrontend, and executes -- but `src/main/`
and `rsp/` are still empty (see `CMakeLists.txt`'s own placeholder-`main()`
fallback), so nothing calls `recomp_entrypoint` or drives the
ultramodern runtime loop yet, and the linker drops the unreferenced
`RecompiledFuncs`/`PatchesLib` object code entirely (hence the ~15KB
binary). Writing that entry point (`bdragoncore/battle-tanx-recomp`'s
equivalent is `src/main/*.cpp`) is the next real step toward the game
actually running, ahead of or alongside PROGRESS.md's items 6-8.

## 2026-09-27, round 21: first clean N64Recomp run -- `N64Recomp battletanxga.us.rev0.toml` exits 0

The big one. Round 20 produced a well-formed config; this round is the
whole debugging loop of actually running it against real `N64Recomp` until
it stopped erroring, function by function. Ends with exit code 0, 1288
functions, 27 output `.c`/`.h` files in `RecompiledFuncs/` (gitignored, not
committed -- regenerate with `N64Recomp battletanxga.us.rev0.toml` from the
repo root once the ROM is in place). Every fix below is applied to
`battletanxga.us.rev0.toml`, `BattleTanxGASyms/battletanxga.us.rev0.syms.toml`,
and the two source `BattleTanxGASyms/*.toml` pieces it's assembled from.

Fixed, roughly in the order N64Recomp's own errors surfaced them:

1. **The whole `[patches] ignored`/`renamed` list from round 19 was
   redundant and actively broke the build.** N64Recomp has its own
   built-in `reimplemented_funcs`/`ignored_funcs`/`renamed_funcs` lists
   (`src/symbol_lists.cpp`, ~440 names) that it applies to matching
   functions *before* it even reads the config's own ignored/renamed
   lists, renaming them to `name_recomp`. Listing a name in both places
   caused "Function X is set as ignored in the config file but does not
   exist!" (it had already been renamed by the time the config-driven
   pass ran). Diffed our 71 names against N64Recomp's built-in set: all
   71 overlap, 0 remaining that need declaring ourselves. Deleted that
   whole section. Any instruction patch or hook targeting one of those 71
   names needed its `func` updated to `name_recomp` to match.
2. **`__libm_qnan_f`** (in the trusted function list from n64sym) turned
   out to be a libm quiet-NaN float *data* constant, not a function ("Unhandled
   instruction: INVALID" trying to disassemble it as MIPS). Removed.
3. **`sync` and `cache`**: N64Recomp's recompiler doesn't implement either
   instruction at all. Nopped every real occurrence (`sync`: 4 icache/
   dcache-init loops; `cache`: 2 more of the same -- the other `cache`
   users, `osInvalDCache`/`osInvalICache`/`osWritebackDCache*`, are in
   N64Recomp's built-in `reimplemented_funcs`, so their bodies are never
   recompiled at all and don't need patches). Finding the real `sync`
   instructions needed a slightly non-obvious scan: its encoding allows a
   nonzero "stype" hint in bits that a naive all-zero-word search would
   miss (confirmed against capstone's decode of the real bytes).
4. **`mfc0`/`mtc0` for anything but cop0 register 12 (Status)**: N64Recomp
   only implements register 12. Everything else ("Unhandled cop0 register
   in mfc0/mtc0: N") is exception-vector dead code under a recompiled
   runtime (EPC/Cause/ErrorEPC reads and writes, and one function that
   dumps every single cop0 register as part of an exception-context save)
   -- nopped 27 mtc0 + 3 eret (round 18) + 41 more mfc0 (this round). Two
   whole functions (`func_8007919C`, `func_80079260`) and a second
   exception-dispatcher copy (`__osException_80104FB0`) had unconditional
   jumps INTO other functions' interiors that no per-function static
   recompiler can handle ("Unhandled branch ... to <mid-function
   address>") -- stubbed all three outright (`[patches] stubs`) rather
   than patching every individual instruction, since none of it can ever
   run under the recompiled runtime anyway.
5. **`movz`/`movn`, trap instructions (`tltu`/`tgeu`), `dmtc0`, `jalr`
   with a non-`$ra` link register**: none of these are implemented by
   N64Recomp's recompiler. `tltu`/`tgeu` were two genuine compiler-inserted
   assertion checks (nopped, safe -- they only ever fire on a bug). Every
   `movz`/`movn`/`dmtc0`/degenerate-`jalr` occurrence turned out to be a
   **data misdecode**, not real code (see next point).
6. **The recurring pattern, by far the most work this round: gap-guessed
   function sizes swallowing whatever came after the real code.** Round
   14's splat-based sizing used "distance to the next known symbol" as a
   function's size, which is only right when nothing sits between two
   real functions. In practice there's often a short string constant, a
   float/jump-table literal pool, or (worse) a completely separate second
   function packed into that same gap. N64Recomp's recompiler disassembles
   a function's *entire* declared byte range, so any of that trailing
   junk being mistaken for code is fatal the moment it decodes into
   something N64Recomp can't handle (or, worse, into something that
   parses as a *plausible-looking but nonsensical* instruction --
   `movn $zero, $zero, ...`, `jalr $zero, $zero`, `j 0x8C000000`, a branch
   whose computed target lands outside this ROM's entire loaded segment --
   which a simple "is this a valid MIPS word" check doesn't catch).
   Resolved ~30 functions this way, in three shapes:
     - **Pure trailing junk, no real second function**: truncate the
       function's size to end right after its own last real `jr $ra`
       return (confirmed by direct disassembly with `rabbitizer`, the
       same MIPS decoder N64Recomp itself uses -- `pip install
       rabbitizer`). ~20 functions (`func_80089E84`, `func_8008A8C4`,
       `func_800A9B64`, `func_800D84DC`, `func_800EAF6C`, `func_800EFC28`,
       `func_800F28AC`, `func_80113E30`, `func_800EA14C`, `func_800CAE10`
       -- this last one the other direction, undershooting by 0x18 bytes
       and cutting off a shared switch-statement epilogue several branches
       target -- and more).
     - **A real second function hiding in the same declared range**:
       split into two symbols, e.g. `func_800E16D8`/`func_800E1BB0`,
       `func_800F38C0`/`func_800F3B80`, `func_800F1770`/`func_800F17B0`,
       `func_800F6650`/`func_800F6C70`, `func_80099784`/`func_80099830`.
       Any instruction patch or hook whose target address moved into the
       second half needed its `func` reference updated.
     - **A handful of ~2-20 byte entries that were entirely fake** --
       n64sym or the round-11 gap scan produced a "function" at an
       address that's actually a pure ASCII string (`func_8011A3F0`,
       `func_8011A484`, `func_8011A4C0` -- literally in-game UI text like
       "Select one Button..." and "...fire All..."), a plain data
       variable (`osViClock`, `__OSGlobalIntMask`, `__osPiAccessQueueEnabled`
       -- real libultra names, but for globals, not functions), or a
       pointer/dispatch table indistinguishable from the round-15 one at
       0x8011a8 (`func_80119628`, `func_8011A5F0`, both in the same
       0x8011axxx-0x8011bxxx neighborhood). Deleted outright.
7. **Missing functions in gaps our round-11 scan never generated a symbol
   for at all** (not a sizing problem -- no entry existed there). Found
   two ways: (a) reactively, from N64Recomp's own `static_0_<addr>`
   auto-analysis when something's `jal` target had no name
   (`func_8007E118`, `func_800A10E0`, `func_800B95C8`/`func_800B99AC`,
   `func_80114470`); (b) proactively, once the pattern was clear enough to
   be worth automating -- swept every gap between two consecutive known
   functions for a clean, valid instruction run ending in a real `jr $ra`
   before waiting for N64Recomp to trip over it (`func_800BB53C`/
   `func_800BBDC0`, `func_800D0070`, `func_800DEEDC`, `func_800EC778`,
   `func_800F1900`). The proactive sweep needed the branch/jump-target
   plausibility check from point 6 (a `j`/branch to an address outside
   this ROM's own 0x80070000-0x80180000 loaded segment is data, not a
   real control-flow edge) to avoid false negatives.
8. **`n_alEnvmixerPull` stubbed, not fixed**: N64Recomp's static analysis
   couldn't determine the size of a computed jump table this function
   uses ("Failed to determine size of jump table at 0x80077720 for
   instruction at 0x80100120" -- the table isn't in this function's own
   byte range, which the analysis requires). This is CPU-side audio
   envelope-mixing code (the software counterpart to the RSP audio
   microcode's own mixing). Stubbed to unblock the build; unlike
   everything else stubbed this round, this one is **not** known-dead
   code and needs real attention once audio is being worked on (item 6 in
   PROGRESS.md).

Net effect on the symbol table: started this round at 1321 trusted
functions (post round-14), ended at 1288 (many 1:1 replacements from
splits, net loses from the outright-fake deletions, net gains from the
newly-found missing functions -- see `git diff` on
`BattleTanxGASyms/battletanxga.us.rev0.syms.toml` for the exact set).

**Tooling note for next time**: `pip install rabbitizer` gives Python
bindings for the *exact* MIPS decoder N64Recomp itself uses
(`rabbitizer.Instruction(word).isValid()`, `.getOpcodeName()`,
`.disassemble()`) -- far more reliable for this kind of validation than
`capstone`, which is more lenient and both missed real problems (e.g. it
happily decoded a `madd` where rabbitizer correctly said `INVALID`, since
VR4300 doesn't have `madd`) and shares the "syntactically valid but
nonsensical" blind spot for data that happens to decode as some real
instruction. Any future symbol-table cleanup should validate against
rabbitizer specifically, not capstone.

## 2026-09-27, round 20: assembled the real battletanxga.us.rev0.toml -- this project has an actual N64Recomp config file for the first time

Merged rounds 13-19's pieces (the symbol table, the 71-entry ignored/
renamed list, the 30 cop0/eret instruction patches, the 101 division
hooks) into one file, `battletanxga.us.rev0.toml` at the repo root,
matching `bdragoncore/battle-tanx-recomp`'s exact structure
(`[input]` / `[patches]` / `[[patches.instruction]]` / `[[patches.hook]]`
in one config). Validated it parses as well-formed TOML and that every
section round-trips to the right counts (Python's `tomllib`: 71 ignored,
71 renamed, 30 instruction patches, 101 hooks).

This is the first time this project has had an actual config file to hand
N64Recomp -- everything before this was symbol-table/patch-list pieces
that hadn't been assembled into the thing the tool actually reads. Still
only covers the first MB's code (see round 17 for why that's believed to
be ~all of it), and still missing the by-inspection stubs
`bdragoncore/battle-tanx-recomp`'s own list has a couple of (not found by
name-matching, so not caught by anything done so far). Running this
config through a real `N64Recomp` build is the natural next checkpoint,
once the toolchain itself is built (`lib/N64ModernRuntime/N64Recomp` per
`BUILDING.md` — not yet done this session, since the submodules aren't
checked out).

## 2026-09-27, round 19: found the real ignored/renamed list -- 71 functions where this ROM's own copy should defer to librecomp

Round 18 deliberately left `stubs`/`ignored`/`renamed` alone rather than
guess at librecomp's API surface. Fixed that properly this round: cloned
`N64Recomp/N64ModernRuntime` and grepped `librecomp/src/*.cpp` for every
function name ending in `_recomp` -- that suffix marks something librecomp
implements natively (139 total, `osInitialize`, `osPiStartDma`,
`__osDisableInt`, the whole `osPfs*`/`osVi*`/`osCont*`/`osFlash*`/
`osEeprom*`/`osVoice*` surface, etc.), not something a recompiled game
should run its own copy of.

Cross-referenced against the trusted symbol table's ~430 n64sym-identified
names: **71 direct matches** -- real functions in this ROM, at real
addresses, that duplicate something librecomp already provides. Generated
`[patches] ignored = [...] renamed = [...]` for all 71
(`BattleTanxGASyms/battletanxga.us.rev0.renamed_ignored.toml`), matching
`bdragoncore/battle-tanx-recomp`'s exact pattern for this (same name in
both lists: `ignored` skips recompiling this ROM's own copy, `renamed`
points calls at librecomp's implementation instead).

**Caveat**: this only covers names n64sym already matched by signature.
`bdragoncore/battle-tanx-recomp`'s own list also stubs functions found by
inspection rather than name-matching (e.g. two cache-invalidate loops the
host doesn't need) -- nothing here does the equivalent search yet, so this
71-entry list is a solid start, not a complete `[patches]` section.

## 2026-09-27, round 18: generated the instruction-level patches N64Recomp's config needs -- cop0/eret nops and guarded div hooks, for real addresses this time

With a trusted, sized symbol table in hand (round 14), did the mechanical
scan `bdragoncore/battle-tanx-recomp`'s own config comments describe as
needed: every `cop0` write and `eret` needs a nop (nothing is emulated),
every `div`/`divu`/`ddiv`/`ddivu` needs a guarded hook instead of running
raw (a real divide-by-zero in the game would otherwise be a host
`SIGFPE`). Decoded these directly from each trusted function's raw
instruction words (same technique as every raw-byte scan this session):

- 27 `mtc0` (cop0 write) instructions, 3 `eret` -- all in places that make
  complete sense (`__osException`, `__osDispatchThread`, `osMapTLBRdb`,
  `__osDisableInt`/`__osRestoreInt`), which is itself a good sign the
  underlying symbol table holds up.
- 101 divisions: 62 `div`, 30 `divu`, 3 `ddiv`, 6 `ddivu`.

Generated real `[[patches.instruction]]` entries for the cop0/eret nops
(`BattleTanxGASyms/battletanxga.us.rev0.instruction_patches.toml`) and
`[[patches.hook]]` entries with the guarded division C for all 101 divides
(`BattleTanxGASyms/battletanxga.us.rev0.div_hooks.toml`), decoding each
instruction's actual `rs`/`rt` operands so the hook text references the
right `ctx->rN` registers rather than being copy-pasted boilerplate.

**Caveat on the div hooks**: the `div`/`divu` (32-bit) hook text exactly
mirrors a confirmed-real pattern from `bdragoncore/battle-tanx-recomp`'s
own config. The `ddiv`/`ddivu` (64-bit) hooks are this project's own
extrapolation -- no 64-bit division example existed in the reference to
confirm the exact syntax/available macros against. Marked inline in the
file; verify before trusting those 9 specifically.

**Not done yet**: `stubs`/`ignored`/`renamed` (which of the ~430
n64sym-identified functions should defer to librecomp's own
implementations instead of being recompiled from this game's copy) needs
real knowledge of librecomp's exact API surface to get right -- guessing
here risks silently wrong config rather than an honest gap, so left for
when that can be checked properly rather than fabricated. Same for the
main `battletanxga.us.rev0.toml` `[input]`/full config file itself, which
these two files are pieces of but don't yet assemble into.

## 2026-09-27, round 17: tested the "code mostly fits in the first MB" hypothesis directly against the second MB -- confirmed. This changes what "finishing the RE" even means for this project.

Round 16 ended on a hypothesis rather than a fact: no evidence of code
past `0x80171000`, so maybe the game's CPU code footprint just mostly fits
in the first automatically-loaded MB. Tested it directly: ran the same
splat/spimdisasm scan (no symbol seeding, since we have none there) across
the entire second MB (rom `0x101000`-`0x201000`, vram `0x80171000`-
`0x80271000`).

**Result: 12 resync points in the whole MB**, versus 2388 in the first
MB -- roughly a 95x drop in apparent code density. Spot-checked the first
one (`func_8017BA38`): the exact same repeating-nibble texture/asset
pattern (`0x6319`, `0x5AD7`, `0x4A53`, `0x4211`) seen everywhere else
non-code data has turned up in this project. It's a false-positive resync
inside pure data, not a real function, and there's no reason to expect the
other 11 are different.

**This confirms the hypothesis rather than just failing to falsify it**:
this game's actual CPU code is concentrated almost entirely in the first
automatically-loaded MB. The remaining ~7MB is overwhelmingly non-code
asset data -- textures, audio samples, level/model data -- not more
undiscovered game logic. Combined with rounds 14-16 (no overlay system, no
DMA-triggered code loading found, PI DMA usage that does exist is
ordinary), the picture that's emerged is a fairly conventional one for an
N64 game of this era: one resident code segment, everything else is
assets.

**This reframes what's actually left to do on this project.** It was
never "reverse-engineer 8MB of unknown code" -- it's "finish mapping
~1MB of code" (already ~85% function-bounded) plus "build asset-extraction
tooling for the other ~7MB" (textures, audio, levels), which is a
different, generally more mechanical kind of work (known N64 texture/
audio formats, not open-ended disassembly). See `PROGRESS.md` for the
updated roadmap reflecting this.

## 2026-09-27, round 16: closed out the "does code reach outside the first MB" question -- no evidence found, likely because there isn't any (yet)

Checked the rest of round 14's 71 remaining suspects. The large ones
(several 0x2000-0x5000 bytes) turned out to be a third category, not
gap-guessed code: raw bytes at the top few (`func_80164CC4`,
`func_8014CFE4`, `func_80160E58`) are the exact same dense repeating-
nibble pattern (`0x63196319`, `0x5AD75AD7`, ...) round 7 originally
flagged as texture/asset data -- not code, not microcode, just ordinary
non-code asset bytes that happened to fall between two known symbols with
nothing splat or n64sym recognized in between.

Checked the small ones (`<=0x200` bytes, the ones most likely to be real
functions with a real external call) individually. All their out-of-range
`j`/`jal`-shaped targets are either suspiciously round addresses
(`0x88000000`, `0x88080000`, `0x8c000000` -- multiples that don't occur in
real code, a signature of a false opcode match against data/padding) or
single isolated one-off hits with no corroborating pattern. None read as
a real call to real code outside the segment.

**Conclusion**: nothing in the first MB's function table -- trusted or
suspect -- shows genuine evidence of code reaching past
`0x80171000`. Combined with rounds 14-15's findings (the suspicious
regions were RSP microcode, a dispatch data table, and plain asset data,
not further code), the most likely explanation is simply that this game's
actual CPU code footprint mostly fits in the first automatically-loaded
MB, with the remaining ~7MB being predominantly assets (textures, audio,
level data) -- not proof, but the working hypothesis until the rest of the
ROM is actually scanned. That scan -- extending the same corrected-header
splat pass past `0x101000` -- is the natural next step now that this
question has a real answer instead of an open loop.

## 2026-09-27, round 15: found a real dispatch-table data structure inside round 14's "suspect" pile; it's the first concrete trace of the message/event dispatcher the 2026-09-19 screenshots hypothesized

Looked closer at round 14's 123 suspect entries rather than treating them
as uniformly bad. The small ones (many exactly `0x20` bytes) turned out to
be a different problem entirely: not gap-guessed code, but a **real data
table misread as a run of tiny functions**. Dumping the raw words at e.g.
`0x8011A280` shows values like `0x80116E80`, `0x80117DDC`,
`0x8011A8DC` -- valid pointers into our own known function range, sitting
as plain data -- interleaved with what my `j`/`jal` opcode scanner
mistook for branch instructions (a data word starting with byte `0x0A`
has the same top-6-bit pattern as a real `j` opcode, by coincidence).

Mapped the actual extent: `0x80118900`-`0x8011B300` (~10.5KB) is ~37%
in-range pointers, consistent with a structured table (pointer/pointer/
flag/zero-style entries) rather than code. A dense, pure 52-pointer run
within it (`0x8011B1A4`-`0x8011B274`) alternates between two tight address
clusters -- `~0x8011A8xx` and `~0x80116Exx`/`0x80116Fxx` -- repeating with
minor variation, which reads like a genuine state-machine/dispatch table
(pairs of e.g. condition-check and action function pointers). This is the
first *concrete, address-level* trace of the "message/event dispatcher"
pattern the 2026-09-19 screenshot session hypothesized from Ghidra's
decompiler view, rather than just a plausible-sounding read of one
function's C-level logic.

**Still doesn't show code reaching outside the first MB**: every pointer
found in this table stays within the known `0x80071000`-`0x80171000`
segment. Moved the 52 addresses this table covers out of the suspect pile
into `syms/battletanx_ga_data_table_0x8011a8.txt` (documented as data, not
carried in the function symbol table at all -- distinct treatment from
round 14's RSP-microcode exclusions, since this is genuine data rather
than a different instruction set). 71 suspects remain genuinely
unclassified -- likely still a mix of real gap-guessed code and more
undiscovered data tables, not yet sorted.

**Where this leaves the "does anything call outside the first MB"
question**: still no confirmed evidence either way. Of the original 123
suspects, 52 are now explained as this data table (in-range) and the
`rspbootTextStart` region as RSP microcode (round 14). The remaining 71
are the only place such evidence could still be hiding, and they haven't
been individually resolved.

## 2026-09-27, round 14: sanity-checked round 13's symbol table -- found and removed 123 gap-guess artifacts, confirmed round 9's RSP microcode hypothesis

Before extending the scan past the first MB, checked whether any code in
round 13's function list calls out beyond the segment (`0x80071000`-
`0x80171000`) -- if the whole game's code fit in the first MB, there'd be
nothing left to chase. Decoding `j`/`jal` directly from raw ROM bytes
within each known function's byte range (not from spimdisasm's text
output, which renders this whole run as raw `.word` throughout -- a
rendering-confidence quirk unrelated to whether the underlying label/
address data is correct, confirmed by re-deriving the exact same 1509
boundaries from it a second time) found real problems, not real overlay
leads:

- `rspbootTextStart` (n64sym's own name!) is full of `j`/`jal`-shaped
  words with nonsensical targets (`0x8c000000`, `0x84001xxx`, ...) --
  because it's genuinely RSP microcode, a different instruction set
  entirely, not CPU code. This **confirms round 9's original hypothesis**
  about this region, which round 11-12 had provisionally walked back after
  finding it densely function-packed under the corrected header. Round
  9 was right about *what* it is; round 10-12 were right that it's
  legitimately resident (both can be true).
- 123 other entries (mostly large `func_XXXXXXXX` placeholders, 0x620-
  0x16F0 bytes each, clustered `0x80118000`-`0x80153000`, plus two n64sym
  *data* symbols wrongly carried as functions -- `__osCurrentTime`,
  `__osTimerList_80134D00`, libultra state variables, not code) show the
  same garbage-jump signature. These are gap-guess artifacts: not enough
  known boundaries in that stretch for splat to have sub-divided it
  correctly, so each absorbed neighboring microcode/data/unfound-function
  bytes into one oversized "function."

Pulled all 123 out to `syms/battletanx_ga_funcs_suspect.txt` rather than
leave them in the trusted table -- N64Recomp would eventually choke trying
to recompile RSP microcode or data as CPU code. Regenerated
`BattleTanxGASyms/battletanxga.us.rev0.syms.toml` with the remaining 1321
entries only (still not independently verified one-by-one, but at least
self-consistent -- every entry's own body only jumps within the segment).

**Real answer to the original question**: after excluding the known-bad
123, the remaining functions collectively contain zero calls leaving the
first-MB segment. Some of what's *in* the suspect list might still reach
outside the segment once properly re-split (can't tell with garbage
boundaries) -- so this doesn't yet prove the whole game fits in 1MB, but
it does mean there's no clean evidence otherwise either. Re-splitting the
suspect region properly (probably needs bounded sub-probing like round 8
did, now under the correct header) would settle it either way, and is
higher-priority than blindly extending the scan into unexplored ROM.

## 2026-09-27, round 13: a real, sized symbol table for the first MB -- BattleTanxGASyms/battletanxga.us.rev0.syms.toml now has actual content

Merged round 11's 1310-function scan with n64sym's ~480 name matches
(preferring the real name where both cover the same address; 20 name
collisions where n64sym matched the same function twice at different
addresses, disambiguated with an address suffix), filtered to the 1509
entries that fall inside the scanned segment (`0x80071000`-`0x80171000`;
52 matches -- fixed low-memory OS state like `osTvType`, plus a few past
the first MB -- saved separately in
`syms/battletanx_ga.symbol_addrs_outofrange.txt` for later), and fed the
result back into splat as `symbol_addrs_path` seed points.

Took three tries to get the run itself right (splat's symbol file format
rejects `#` comments outright; a duplicate name from two n64sym matches
needed disambiguating; two overlapping background invocations sharing one
log path raced and produced a corrupted-looking "complete" log while the
real job was still running for another 8+ minutes -- killed the stray and
reran clean with a fresh log path). Once it ran cleanly: all 1509 seeds
got individual labels with correct sizes (computed from the gap to the
next known boundary), 430 with n64sym's real name instead of a
`func_XXXXXXXX` placeholder.

Split the result into 1444 likely-code entries and 65 data-shaped ones
(`_rodata_`/`_bss_`/`D_`/`jtbl_`-style names, filtered out by pattern --
N64Recomp's `functions` array should only ever list actual code, not
data, or it'll try to recompile rodata as MIPS instructions). The 1444
code entries are now in `BattleTanxGASyms/battletanxga.us.rev0.syms.toml`,
in the actual format N64Recomp expects -- the first time this file has
had real content instead of being an empty placeholder.

**Caveats, spelled out in the file's own header**: sizes are gap-to-next-
known-symbol, not confirmed function ends -- an unfound real boundary
between two known symbols would make the earlier one's listed size too
large. Names ending `_text_XXXX` mark an n64sym signature match at an
offset *inside* a larger function, not necessarily a real separate
function start. Still only covers the first MB (`1/8` of the ROM). Treat
this as a strong first pass, not a verified split -- the next real step
(beyond extending coverage past the first MB) is spot-checking a sample of
these against real disassembly the way round 8/10 did for individual
functions, now that doing so isn't fighting a wrong header.

## 2026-09-27, round 12: confirmed -- the overlay mystery was entirely the wrong header. There is no overlay system here (at least not in the first MB).

Round 11's queued corrected-header scan finished: a single splat pass over
the full first MB (ROM `0x1000`-`0x101000`) recovered **1310 real function
boundaries** via spimdisasm re-syncing, running continuously from the crt0
entry (`0x80071000`) through to `0x8016FC5C` -- past both the old "overlay
wall" and the full "overlay blob" range with no gap, no renewed swallow,
nothing resembling a wall at all.

Checked function density specifically in the two ranges this entire
investigation has spent the most effort on:

| Region | Functions | Density |
|---|---|---|
| Old "overlay wall" (`0x80105000`-`0x80106500`) | 18 | 3.43/KB |
| `OverlayScan6`'s claimed "overlay blob" (`0x800F8000`-`0x80112000`) | 302 | 2.90/KB |
| Whole scanned window (average) | 1310 | 1.28/KB |

Both "mystery" regions are **denser** with clean function boundaries than
the scan's own average -- the opposite of what non-code/garbage data would
look like. There is no overlay wall, no overlay blob, no custom
per-object-type loading slot at `0x800F81CC`. Every round-7-through-9
finding framed around "why does this region look like garbage" was
answering a question created entirely by scanning the wrong ROM bytes.
This also retroactively explains why `OverlayScan1`-`6` (2026-09-19,
Ghidra) never found a loader despite exhaustively searching for one
(direct calls, stored pointers, register-loaded indirect calls, hardware
DMA register use): there wasn't one to find in that range. Their own
Ghidra project may have had a comparable mapping issue for this address
range, or simply never got a working disassembly of it at all -- either
way, "no loader found" was the correct result, just not for the reason
anyone thought at the time.

**Saved**: the full 1310-function list is now in
`syms/battletanx_ga_funcs_round11.txt` (vram addresses only, no sizes yet
-- next real splat pass should use these as seed points via
`symbol_addrs_path` to get a properly bounded, per-function split instead
of one giant swallowed file). This is the first genuinely trustworthy,
broad function-boundary dataset this project has produced.

**Reframing what's actually left to do**, now that the phantom is cleared:
this project doesn't have a special overlay-loader mystery to solve. It
has the completely ordinary (if large) task any from-scratch N64 recomp
has: turn 1310 anonymous `func_XXXXXXXX` addresses into a real symbol
table (start + size + eventually names), find the actual entrypoint/boot
sequence details needed for N64Recomp's config (stubs, instruction
patches, stock-runtime compat shims -- see `PROGRESS.md`), and only then
start standing up the N64Recomp build proper. Round 11's DMA-registers
finding (PI hardware DMA is real and used ~59 places) remains true and
useful independent of the overlay question -- osPiStartDma-family calls
are ordinary and expected in any N64 game, not evidence of anything
exotic.

## 2026-09-27, round 11: the "overlay" mystery itself may be a phantom -- two more pieces of convergent evidence

Following up on round 10's header fix. `OverlayScan6.java` (never actually
run, but its file header documents what `OverlayScan1-5` already found)
places "the overlay blob" this whole investigation has been hunting a
loader for at **VRAM `0x800F8000`-`0x80112000`** -- which is, almost
exactly, the range round 9's n64sym scan identified as ordinary resident
libultra: `rspbootTextStart`, the `Mus*`/`al*` audio library, `osViBlack`,
`guMtxIdent`, `memcpy`, `sqrtf`, `osStartThread`, dozens more, all with
clean signature matches under the corrected header. If that whole range is
just the normal OS library (which round 10 already directly byte-verified
for part of it -- `__osDisableInt`/`__osRestoreInt`), there was never
custom game code there needing a loader in the first place. Worth treating
"there's a custom overlay system in this game" itself as unconfirmed again,
not just the specific addresses examined so far.

**Second, independent thread: re-checked the "DMA hardware registers are
confirmed unused" claim from the 2026-09-19 entry**, since `OverlayScan6`'s
own file header repeats it as settled ("already confirmed the four real
N64 hardware DMA-trigger registers... have ZERO references anywhere in
this ROM"). Wrote a from-scratch, mapping-independent scanner (works
directly on raw ROM bytes, no header assumption needed) for `lui`+`ori`/
`addiu` pairs constructing a full 32-bit address, mirroring
`OverlayScan6`'s own technique. Result matched their claim exactly at
first: zero hits for `PI_DRAM_ADDR`/`PI_CART_ADDR`/`PI_RD_LEN`/`PI_WR_LEN`
(the four DMA-trigger registers), five hits for `PI_STATUS` alone (the
polling register, not a trigger).

But then checked for just a bare `lui $reg, 0xA460` (a PI-register-space
base pointer), **without** requiring an immediate second instruction
completing the same register into one specific address -- and found **59**
occurrences across the ROM, clustered in groups (e.g. seven around rom
`0x4D0`-`0x6F8`, several around `0x95368`-`0x961F4`). This is exactly what
compiled code looks like when it loads a PI-register-block base pointer
once (`lui $t0, 0xA460`) and then hits `PI_DRAM_ADDR`/`CART_ADDR`/`RD_LEN`/
`WR_LEN` via small fixed offsets from that one register (`sw $v0,
0x0($t0)` / `sw $v1, 0x4($t0)`, etc.) -- a pattern the original
lui+ori-pair search (both `OverlayScan6`'s and my own first attempt) is
structurally blind to, since no single instruction pair completes a full
address for those specific registers. **The "DMA is confirmed unused"
conclusion looks like a false negative from a search technique that only
checked one addressing pattern, not an actual absence.** Real PI hardware
DMA is very likely used somewhere in this game after all (consistent with
this ROM's OS library including `osPiStartDma`/`osEPiStartDma`/
`osPiRawStartDma`, all real per round 9's n64sym scan) -- which reopens
the possibility that ordinary `osPiStartDma`-style loading, not an exotic
KSEG1 read-loop, is how any real overlay/asset system here works, if one
exists at all.

**Queued**: a corrected-header splat scan of the full first MB is running
(background, started this round) to get a real function-boundary map like
round 8 tried to build, but on the right bytes this time -- results not in
as of this entry. Once it lands, the actual next step is tracing
`func_8009ED9C`'s real call graph under the corrected header and checking,
address by address, whether any of those `lui $reg, 0xA460` sites sit
inside code that's actually reachable from the game's main loop (as
opposed to, say, buried inside seldom-hit save/EEPROM code) -- that's what
would distinguish "the game uses ordinary PI DMA for something mundane"
from "this is how levels/assets get loaded."

## 2026-09-27, round 10: the ROM-to-RAM mapping every prior round used was wrong -- found and fixed

**This is the most important entry in this file.** Every specific ROM
file-offset claim in every entry below (main_probe's "confirmed 19KB
block," the "overlay wall," `SUB_80087570`'s trampoline, everything in
`syms/screenshot_recovered_funcs.txt`) was computed with the wrong
rom<->vram formula. The *vram* addresses and the reasoning about what
functions call what are mostly unaffected (they came from Ghidra
screenshots or from `jal`/`j` instruction encodings, neither of which
depends on this formula) -- what's wrong is specifically "which ROM file
offset holds the bytes for vram X," which is exactly the thing splat needs
right to disassemble anything correctly. See `syms/rom_info.md` for the
corrected formula and `tools/splat.yaml` for the fixed segment config.

**How this surfaced**: round 9's n64sym scan (built while checking the
reference projects Matt pointed at) returned matches like `__osDisableInt
= 0x80105DB0` -- the exact vram address round 7 had already probed and
called "texture-like data, an overlay slot." Rather than trust either
tool, checked the raw ROM bytes directly:

- At the old-header rom offset for `0x80105DB0` (`vram - 0x7FFFF400` =
  `0x1069B0`, what round 7 and round 8 both probed): genuinely garbage --
  `33333335 6BCDF677 77888AAC ...`, the same repeating-nibble pattern
  round 7 originally flagged. Round 7 wasn't wrong that this specific spot
  is garbage.
- At `vram - 0x80070000` = `0x95DB0` instead: `40086000 2401FFFE 01014824
  40896000 31020001 00000000 03E00008 00000000` -- `mfc0 $t0,$12 / addiu
  $at,$zero,-2 / and $t1,$t0,$at / mtc0 $t1,$12 / andi $v0,$t0,1 / nop /
  jr $ra / nop`. That's not a plausible-looking coincidence; it's the
  textbook compiled form of `__osDisableInt`, instruction-for-instruction.
  `0x80105DD0` (`__osRestoreInt`) matches just as exactly at the
  corresponding offset under the same header.

Where `0x80070000` comes from: `shygoo/n64sym`'s own source
(`src/n64sym.cpp:140`) computes ROM-mode header displacement as
`entryPoint - 0x1000`, reading `entryPoint` from the ROM header itself
(`0x80071000` here, per `syms/rom_info.md` -- not assumed). This game
apparently doesn't use the "boot loads to a fixed 0x80000400" convention
every round of this investigation (including the 2026-09-18 entry that
found the original "crt0 stub") assumed without checking against the
ROM's own header field.

**Closing the loop -- re-examined the crt0 stub itself**: the exact same
physical bytes at rom `0x1000` that round 1/2 originally read (stack
setup, a BSS-clear loop, then `jal 0x8009ED9C`) decode identically under
either header, since none of that depends on the rom<->vram formula --
only the *label* attached to them changes. Under the corrected header
those bytes are `lui $sp,0x8022 / addiu $sp,$sp,-0x1F48` (`$sp =
0x8021E0B8`), a BSS-clear loop for `0x80127E30`-`0x803B17B0`, then `jal
0x8009ED9C` -- and **that address's own first call, under the corrected
header, resolves to `osInitialize`** (vram `0x80105B10`, matching n64sym's
match for that address exactly). A very-first-boot-function calling
`osInitialize` immediately is exactly what should happen; under the old
header this same crt0 is still real (round 1/2 read it correctly), but
its label was wrong -- it genuinely runs at vram `0x80071000`, this ROM's
declared entry point, not `0x80000400`.

**Scale check**: a broad splat scan of the first 320KB under the corrected
header recovered 526 real function boundaries (spimdisasm re-syncing
throughout) -- meaningfully denser than the ~1136-in-~1MB the old header
produced, consistent with the corrected header actually being right rather
than both being comparably-accidental.

**What this means for everything else in this file**: round 7's "overlay
wall" at `func_80105DB0`/`func_801055CC`/`func_80105DD0` is resolved --
there was no overlay mystery there, splat was just being pointed at the
wrong bytes the whole time. Round 8's specific findings about
`SUB_80087570`, `func_800F81CC`, the object-pool functions, etc. all need
to be re-derived from scratch under the corrected header before being
trusted -- they may still turn out to be roughly right (the *vram*
addresses came from Ghidra, independent of this bug), but their content
was read from the wrong bytes, so treat every specific claim about what's
*at* those addresses as unconfirmed again. `syms/screenshot_recovered_funcs.txt`
carries a warning to this effect now.

**Next step**: redo round 8's bounded-probe methodology under
`tools/splat.yaml`'s now-corrected `resident` segment, starting with the
genuinely still-open question -- what does `func_8009ED9C` (now confirmed
to really be the game's first substantial function) actually do, and does
tracing its real call graph (not the wrong-header one) lead anywhere near
an actual overlay/asset-loading mechanism this time.

## 2026-09-27, round 9: checked reference projects Matt pointed at -- a real methodology gap, and the right splat feature for the overlay slot

Matt asked to check the original BattleTanx's recomp repo for reusable
names, and separately pointed at
[RevoSucks/BMHeroRecomp](https://github.com/RevoSucks/BMHeroRecomp)
(Bomberman Hero) as an example of this toolchain done well. Both led
somewhere more useful than literal names.

**bdragoncore/battle-tanx-recomp has no game-specific names to borrow.**
Checked its full symbol table: every non-generic name in it (`osCreateThread`,
`sprintf`, `memcpy`, `cosf`, ~150 total) is a standard libultra/libc name,
almost certainly auto-identified by a signature-matching tool rather than
found by hand -- there is not one manually-named game-specific function
(no `player_update`-style name anywhere). So there's nothing to transplant
address-for-address (the two games don't share code layout anyway), but it
pointed at the actual reusable thing: the *tool* that generates exactly
that kind of match automatically.

**Found and built that tool: `n64sym`** (https://github.com/shygoo/n64sym).
Ships a built-in signature database covering OS 2.0c through 2.0L
`libultra`/`libgultra`/`libleo`/`libnos`/audio libraries, matches them
against a ROM by compiled-instruction signature (tolerant of relocations),
and can emit results directly in splat's `symbol_addrs.txt` format. Built
cleanly in this sandbox (`make n64sym`, plain g++/make, no special
dependencies) and kicked off a thorough scan
(`n64sym rom/battletanx_ga_usa.z64 -s -t -f splat -o ...`) against the GA
ROM -- if this finds real matches, it should identify a good chunk of GA's
own libultra surface automatically, the same way bdragoncore's ~150 names
likely got found. Results not in yet as of this entry; check the next one.

**BMHeroRecomp turned out to be a bigger methodological finding than
expected: it's not a from-scratch reverse-engineering effort at all.**
It's built on top of an existing, separate **full matching decompilation**
project, [bomberhackers/bmhero](https://github.com/bomberhackers/bmhero)
(splat + `asm-differ`, the standard N64 decomp workflow -- rewrite each
function in C until it compiles back to byte-identical machine code),
and only uses that decomp's headers/function definitions where needed for
patches. That's a fundamentally stronger foundation than anything possible
here: **no decompilation project exists for BattleTanx: Global Assault**
(confirmed by the 2026-09-18 entry's own README research, and nothing
found since contradicts that). So this project is necessarily doing the
harder, lower-rigor tier of recomp -- closer to what
`bdragoncore/battle-tanx-recomp` itself did (address+size symbols only,
no matching decomp behind it) -- which is a real, previously-shipped
approach, just slower and more error-prone without a full decomp's
byte-level verification to catch mistakes. Worth being upfront about that
gap rather than implying this project has BMHeroRecomp-level rigor.

**The concretely useful part**: bomberhackers/bmhero's `splat.yaml` shows
the correct splat feature for exactly the "fixed VRAM slot with swappable
content" pattern round 8 (below) found for GA at `~0x800F81CC`:
`exclusive_ram_id: overlay`. Multiple segments, each with a different ROM
`start` but the *same* `vram`, tagged with a shared `exclusive_ram_id`,
tell splat these are mutually-exclusive overlays sharing one VRAM window.
Bomberman Hero has 59 such overlay segments across 8 distinct VRAM slots
(the largest hosting 36 different overlays -- almost certainly one per
level or actor type). This is the right target shape for GA's own
`splat.yaml` once the loader/mapping table is found (round 8's queued next
step: trace what writes into the `800F81CC` slot) -- convert whatever
table is found into one `exclusive_ram_id: overlay` group per distinct
VRAM destination, matching this pattern rather than inventing a new one.

## 2026-09-27, round 8: verified the transcribed leads directly against the ROM -- mixed results, but a real structural finding

Went back to the ROM with splat/spimdisasm (confirmed working in this
sandbox per round 7 below) to directly verify the addresses transcribed
from screenshots in the entry below, rather than trusting the transcription.
Method: bind small splat segments exactly at each address of interest
(same technique round 3 originally used for `main_probe`), since the whole
first-MB region swallows into one giant unreturning blob otherwise --
confirmed again this round (scanning ROM `0x1000`-`0x1061CC` as one
segment recovered 1136 real function boundaries via spimdisasm re-syncing
partway through, but a ~455KB span from `0x8000859C` never re-synced and
had to be probed individually).

**Corrections to the 2026-09-19 transcription** (my own transcription
error, not the original session's -- I conflated an instruction's address
with its enclosing function's address when reading the screenshot):
`0x8002D40C` and `0x8002D4DC` are not function starts. They're a `jal` and
an epilogue instruction respectively, inside two unrelated, ordinary
vector-math functions: `func_8002D3AC` (distance/normalize: `sqrt(x^2+y^2)`
then divide) and `func_8002D468` (accumulate a delta into a stored
position: `pos += delta`). Likewise `func_80027DF0` is real and matches
its transcribed address exactly, but its actual content -- iterating
`D_80216FD0`, decrementing counters, calling `func_80107F10`/
`func_80108078`/`func_8010F6D0` -- reads like generic scheduler/thread
queue cleanup, nothing to do with the asset table (`DAT_800a3b14`) the
transcription associated it with. Net effect: the specific claim "these
call sites read the asset table" doesn't hold up for these three
addresses. `FUN_80048EB0` *did* verify correctly -- it genuinely calls
`func_800F80E8` (see below), matching the transcription exactly.

**What's confirmed real and matches** (all read directly off clean,
individually-bounded splat output, not transcribed):
- `func_80087570` -- confirmed real, exactly as transcribed. But its
  entire body is just `j func_800F81CC` with `sw $v0, 0x0($a2)` in the
  delay slot: a 2-instruction tail-call trampoline, not a "distance
  check" itself.
- `func_80087A80` -- allocates a slot from a 592-byte-stride table
  (`D_80235F00`, indexed by an 8-bit type tag), fills it from another
  object's position fields, and stamps it with the current frame counter
  (`D_8021945C`) at offset `0x2C`. One type value (`0x7F`) short-circuits
  with `j func_800F8700` instead. Reads like an object/effect pool
  allocator.
- `func_80087EF0` -- reads that same `0x2C`-offset frame stamp, computes
  `D_8021945C - stamp`, and compares against `0x1E` (30) before calling
  `func_800AD14C` with args from offsets `0xC`/`0x10`/`0x24` of the
  record. **This matches `FUN_80087eac` from the uploaded `dec1.txt`
  almost exactly** (same `func_0x800ad14c` call, same `+0xc`/`+0x10`/
  `+0x24`/`+0x2c` offsets, same 30-frame-style threshold pattern) --
  they're sibling functions on the same object-record layout. Read
  together, this is an object/effect spawn-and-cooldown system: allocate
  a slot, stamp its spawn frame, later check elapsed frames before acting
  on it. "Distance-gated" in the 2026-09-19 entry's framing was most
  likely describing this frame-age gate, not a ROM/overlay distance --
  **the "loader entry point" read on `SUB_80087570` looks like a wrong
  turn**, not the overlay mechanism.

**The actual structural finding this round**: followed `func_80087570`'s
tail-jump to its target, `func_800F81CC`. It's **not valid code** -- it
disassembles to the same kind of dense, repeating-nibble garbage
(`0x4A534211`, `0x5AD75AD7`, `0x52955295`, ...) that round 7 originally
found at the `probe_105cc`/`probe_105db` "overlay wall" addresses, plus a
long run of zero-padding. Same for `func_800F8700`, the fallback target
`func_80087A80` jumps to for type `0x7F`. **Both addresses are still well
inside the first automatically-loaded MB** (`0x800F81CC`, `0x800F8700` <<
`0x80100400`), which the 2026-09-27 (round 7) entry below hadn't
anticipated -- that entry's "past the 1MB boundary" framing for the
overlay wall doesn't hold here. The more precise picture: there's a fixed
VRAM code slot around `0x800F81CC`-`0x800F87xx` that legitimately holds
non-code data most of the time and gets overlay-loaded with real,
per-object-type handler code at runtime, with stable call-through points
(`func_80087570`, the `func_800F8700` fallback) that always exist and
just jump into whatever's currently loaded there. This is a materially
different (and more specific) theory than either "overlay slots only
exist past 1MB" (round 7) or "DMA is unused, it's a KSEG1 read loop"
(2026-09-19 entry) -- it doesn't resolve the open DMA-vs-no-DMA
contradiction from that entry, but it does explain why a fixed, frequently
-called address can be garbage at one moment (as found here) and real code
at another (as the `dec1.txt`/`ss.txt` decompiles, presumably captured
while something legitimate *was* loaded there, suggest).

**Not yet done**: `func_80087A80`'s type-indexed table (`D_80235F00`,
592-byte stride) is the natural next place to look for how a type maps to
what gets loaded into the `800F81CC` slot -- haven't traced what writes to
that VRAM range yet, which is the actual loader.

## 2026-09-19 (transcribed 2026-09-27): recovered findings from screenshots -- a real overlay/asset table, and a direct link to the "wall" functions

**This entire section was reconstructed from screenshots** Matt uploaded to
this repo (`memorymap.png`, `scree2/4/5/6.png`, `screenpart.png`, `1.png`,
`3.png`, `4.png`, `new1/2/3/34/345.png`, and others), not from any file.
They show a Ghidra session (project `globalrecomp`, imported successfully
with the N64 loader by Warranty Voider -- MIPS:BE:64:64-32addr, o32,
Ghidra 12.1.2) and, in the `new*.png` files, a chat with a Claude Code
session on Matt's own machine driving that Ghidra session interactively,
timestamped the morning of 2026-09-19 -- i.e. **after** the 2026-09-18
entry below, and with real progress that entry doesn't mention. That
session said more than once that it was writing its findings into
`STATUS.md` as it went; that version of the file was never uploaded here,
only these screenshots were, so this section is a best-effort reconstruction
of what it must have said. Treat the addresses/values below as read off
screenshots, not verified against the ROM directly.

**Confirmed real, non-splat-block functions found via Ghidra's own
analysis** (separate from -- and in some cases earlier in VRAM than -- the
19KB block splat confirmed on 2026-09-18):

- `FUN_80007078` (VRAM `0x80007078`) -- calls `FUN_80078e34` and
  `FUN_80078e40`. Real, clean code, well before the splat-confirmed block.
- A cluster of functions (`FUN_80095470`, `FUN_80095b68`, `FUN_80095b90`,
  `FUN_80095cf0`, `FUN_8009c9c0`, `FUN_8009ca10`, `FUN_8009cb40`,
  `FUN_8009cc20`, and more -- 20+ total per Ghidra's own xref list) that
  all read or write `PI_STATUS` (the real N64 Peripheral Interface DMA
  status register, `0xA4600010`). `FUN_80095b68` decompiles cleanly:
  ```c
  void FUN_80095b68(void) {
      do {} while ((PI_STATUS & 3) != 0);   // wait for PI DMA/IO idle
      uStack00000018 = PI_STATUS;
      ASIC_BM_STATUS = *(undefined4 *)(in_stack_0000001c + 0x10);
      FUN_801067fc();
      PI_STATUS = 2;                         // clear/ack
      DAT_80126e60 = DAT_80126e60 | 0x100401;
  }
  ```
  This is a textbook PI-DMA wait/kick routine -- real hardware DMA *is*
  used somewhere in this game, contrary to the "DMA unused" note below.
  Reconciling that contradiction is unresolved (see "Open contradiction").
- **`FUN_80095470` calls `FUN_80105dd0` directly** -- one of the three
  "overlay wall" functions from the 2026-09-18 entry
  (`func_801055CC`/`func_80105DB0`/`func_80105DD0` there, same addresses,
  Ghidra's auto-naming just capitalizes differently). This is the first
  real evidence connecting the wall functions to the PI/DMA status-handling
  code, rather than just being called blind from `func_8009ED9C` as the
  2026-09-27 entry above found.
- `FUN_8009f3b4`: checks flag `DAT_80126ed0`, conditionally calls
  `FUN_80110750` and clears the flag.
- **`FUN_80110750` and `FUN_800f80e8` both fail to decompile** -- Ghidra
  reports "Control flow encountered bad instruction data" and falls
  through to `halt_baddata()`. Cross-referenced from `FUN_8009f3b4`,
  `FUN_80048eb0`, and `FUN_80087664`. These read exactly like the "overlay
  slot" symptom from the 2026-09-18 entry (garbage at the naive address),
  but now with real, confirmed callers instead of just a bare `jal` in
  isolation.
- `FUN_800923b0`: calls `FUN_80105db0()` (another wall function, no args),
  stores the result, and compares it against an internal ROM header
  pointer -- reads like a "re-initialize if the header pointer changed"
  guard, i.e. more real control flow built around the wall functions.

**The actual overlay/asset table, found by address, not guessed:**
tightly packed 8-byte entries of `{ uint32 size; uint32 romAddr; }`
starting at `DAT_800a3b14` (values noted: size `0x0000349B` + a romAddr
around `0x8059xxxx`, size `0x00000AAC`, size `0x00000970`, and more -- a
couple KB to ~20KB each, i.e. **individual asset chunks, not one big
overlay blob**). Real Ghidra xrefs, not guesses:
- `DAT_800a3b14` read by `FUN_8002d4dc` (at ROM/ref `0x80028240`)
- `DAT_800a3b1c` / `DAT_800a3b2c` read by `FUN_8002d40c` (at
  `0x80026d20` / `0x80026d24`)
- `PTR_DAT_800a3b38` / `DAT_800a3b3c` read by `FUN_80027df1`

The working theory in that thread: each call site loads one specific known
asset by passing its `(size, romAddr)` pair into a **shared copy/decompress
routine**, and that shared routine -- not any of these specific call sites
-- is the real target, since it's very likely the same primitive the
overlay loader itself uses.

**A jump-table misdecoding theory for the "bad instruction data" crashes**:
`FUN_80087570` (also referred to as the "distance-check / loader entry
point" -- see below) has an unresolved register-indirect jump (`jr $reg`)
right after what looks like a critical-section guard
(`getCopReg(2, 0x3010)` / `getCopReg(2, 0x300e)`). Ghidra doesn't
recognize the case-address table this jump presumably reads from as data,
so it tries to disassemble those bytes as instructions and produces
exactly the "Unimplemented instruction" / "bad instruction data" garbage
seen at `FUN_80110750`, `FUN_800f80e8`, and `FUN_80087570` itself. If
right, the fix is marking those byte spans as data instead of code, not
finding a DMA loader.

**A dispatcher lead pointing at the real activation code**: a
message/event dispatcher (a common N64-era pattern -- one function fielding
many per-object event IDs) has a case that calls three functions back to
back: `SUB_80087570` (the distance-gated loader entry point being traced),
then `SUB_80087ef0`, then `SUB_80087a80`. The latter two hadn't shown up
before that point and were flagged as strong candidates for the code that
actually performs the overlay copy/activation, since `SUB_80087570` itself
only looked like a distance check plus a call onward.

**Open contradiction, unresolved**: that thread stated "the actual DMA
hardware registers are confirmed unused anywhere in this ROM, so the real
overlay copy is almost certainly a plain software loop reading straight
from the memory-mapped cartridge space (`0xB0xxxxxx`, direct-mapped per
Ghidra's own memory map, no DMA controller involved) rather than a
hardware-triggered transfer." That directly conflicts with the confirmed
`PI_STATUS`-using functions above, which unambiguously do use real PI DMA
registers. Possible reconciliations, neither confirmed: (a) that claim was
scoped to the specific asset-table loader being traced at the time, while
PI DMA is genuinely used elsewhere (e.g. controller pak / EEPROM save
access, matching `patches/README.md`'s boot-race note about `osContInit`),
or (b) the claim needs re-checking against `FUN_80095b68`/`FUN_80095470`
directly. Worth resolving before trusting either theory fully.

**Queued next steps in that thread** (not yet done, per the screenshots):
decompile `SUB_80087ef0`, `SUB_80087a80`, `FUN_8002d40c`, `FUN_8002d4dc`,
and `FUN_80027df1`; and fix the `FUN_80087570` jump table by marking the
misread span as data.

**Also recovered, likely unrelated to the overlay question**:
`FUN_800a1b80` and neighbors look like a `%`-escape string formatter
(printf-style); `FUN_80048eb0` parses a byte-packed, `\x02`-terminated
value out of `DAT_80219590`/`91`/`92`, reading like a compressed
command/animation-list decoder. Both are real, confirmed code, just not
obviously part of the loading path.

## 2026-09-27: splat now runs in a cloud sandbox; the "overlay wall" is probably not overlays

Picked up the prior scaffold and analysis (uploaded by Matt to this repo's
`main` branch as archives) and continued from where the 2026-09-18 entry
left off.

**The PyPI/npm/crates.io block from the previous entry is not universal.**
In this session, `pip install splat64[mips]` (which pulls in `spimdisasm`
and `rabbitizer`, splat's real dependency chain) worked with no issues,
after two packaging speed bumps: `pylibyaml`/`intervaltree`'s legacy
`setup.py` fails to build under a too-new `setuptools` (`AttributeError:
install_layout`) unless you pin `setuptools<60` in a venv first, and the
plain `pip install splat64` alone leaves `spimdisasm`/`rabbitizer`/`n64img`
unresolved even though splat needs them at import time -- `splat64[mips]`
pulls the full set. Once installed, `python3 -m splat split tools/splat.yaml`
ran cleanly and reproduced the confirmed 19KB resident block
(`0x9F99C`-`0xA449C` ROM, VRAM `0x8009ED9C`-`0x800A389C`) from the
2026-09-18 entry exactly, plus function-split suggestions splat had never
gotten to run before.

**Read `func_8009ED9C` in full** (the very first function `crt0` calls, and
per the round-6 findings the first instruction of the confirmed resident
block). It:

- Wraps its body in a call to `func_80105DB0` (return value kept in `$s0`)
  at the top, and `func_80105DD0` (called with that same value in `$a0`) at
  the very bottom -- a plain acquire/release or begin/end pair, not
  something that reads like a DMA kickoff.
- In between, does list/queue-style manipulation on two globals,
  `D_80126EF0` and `D_80126EE8` -- consistent with the "list-traversal/
  dispatch logic" read from the round-3 notes.
- Also calls `func_80110EE0` and `func_801056CC` in passing, both similarly
  unconditional.

**Why this matters**: `func_80105DB0`, `func_801055CC`, `func_80105DD0` and
`func_80110EE0` all sit at VRAM addresses past `0x80100400` -- the edge of
the ~1 MiB block the N64's IPL3 bootcode loads automatically for the
standard CIC chips (6101/6102/7101/7102), which is also, not coincidentally,
almost exactly where the confirmed-clean resident block ends
(`0x800A389C`). Code past that boundary normally has to be DMA'd in
explicitly by the game before first use. But here, `func_80105DB0` is
called completely unconditionally in the first few instructions of the
*first function the game ever runs* -- and `crt0` itself is only 14
instructions (set `$sp`, clear BSS, `jal 0x8009ED9C`), leaving no room for
a loader call before that. That's hard to reconcile with "this is a
runtime-loaded overlay slot," which was the working theory in the
2026-09-18 entry after round 7 found texture-like garbage at the naive
linear ROM offset for these addresses.

Checked one alternative explanation and ruled it out: if there were a
static jump/overlay table listing these functions' real ROM locations as
raw data words, the literal 4-byte big-endian value for their VRAM address
(e.g. `80 10 5D B0`) ought to show up somewhere in the ROM as data. It
doesn't -- not for any of `func_80105DB0`, `func_801055CC`,
`func_80105DD0`, `func_80110EE0`, `func_801056CC`, or even `func_8009ED9C`
itself (checked all six against the full 8 MiB ROM). That's expected for
plain `jal` call sites (the encoding doesn't carry a full 32-bit address),
so it doesn't disprove an overlay table, but it does rule out "there's an
easy-to-find table of raw pointers to grep for."

**Leading hypothesis now**: the simple `rom_offset = vram - 0x7FFFF400`
mapping that correctly located the confirmed 19KB block probably just
stops applying past that block for a structural reason -- padding, a
second linked segment, or a non-contiguous section layout the linker
produced -- rather than these specifically being demand-loaded overlays.
Round 7's "texture-like data" finding at the naive offset would then mean
"wrong offset," not "not code." This doesn't yet rule the overlay theory
back in either; it's genuinely unresolved.

**Not attempted this round**: downloading and running Ghidra headless
against `tools/OverlayScan6.java` (the script from 2026-09-18, written for
exactly this search). Java 21 and network access are both available in
this sandbox, so it's likely feasible here too, but pulling down a Ghidra
distribution is a bigger, slower step worth checking with Matt about
before spending the time.

### Consolidated into the repo this round

- `syms/rom_info.md`, `syms/undefined_funcs_auto.txt`,
  `syms/undefined_syms_auto.txt`, `tools/*.py`, `tools/OverlayScan6.java`,
  `tools/splat.yaml`, `battletanx_ga.ld`, `src_probes/*.c` (renamed from
  `src/` to avoid colliding with the N64Recomp-style `src/` this repo also
  has, from an earlier, more N64Recomp-build-centric scaffolding pass done
  in parallel -- see below) -- all from the 2026-09-18 upload.
- `tools/symbols_to_n64recomp_toml.py` -- new. Converts a Ghidra CSV export
  or a splat-style `name = 0xADDR;` list into the `[[section]].functions`
  TOML array N64Recomp's own symbol file format expects (see
  `bdragoncore/battle-tanx-recomp`'s `BattleTanxSyms/*.syms.toml` for the
  target format). Not yet run against anything real -- there's no confirmed
  full function list yet to feed it.
- The ROM (`rom/battletanx_ga_usa.z64`) and the large raw probe dumps
  (`assets/unk_*.bin`, several megabytes each, essentially fragments of the
  ROM itself) were deliberately **not** brought into this repo's tracked
  tree -- see "Heads up" below.

### Also present in this repo: a from-scratch N64Recomp-style scaffold

Before finding this uploaded work, a parallel scaffolding pass (same
session) set up `CMakeLists.txt`, `.gitmodules` (N64Recomp,
N64ModernRuntime, RecompFrontend, rt64), `patches/`, `include/`, and
`BattleTanxGASyms/` following `bdragoncore/battle-tanx-recomp`'s structure
directly -- the build-system side of what this project will eventually
need, once real symbols exist. That's still in the repo and still correct;
it just hasn't been exercised against anything yet, since the symbol table
it expects doesn't exist. The splat-based work above is the actual path to
producing that symbol table. See `PROGRESS.md` for that side's status.

### Heads up: the ROM ended up in this repo's history

The uploaded `battletanx-recomp.7z` / `battletanx-recomp-scaffold.zip`
archives (commits `d1abf2d` and `52b0132` on `main`) contain the full ROM
dump and several multi-megabyte raw excerpts of it, because they're
straight archives of a working directory that had those files present
locally (correctly gitignored *within* that nested project, but the
archive tool doesn't know about `.gitignore`). They're sitting in this
repo's git history on GitHub now. Worth deciding whether to scrub that
history (e.g. rewriting `main`, or just deleting-and-force-pushing once
the useful bits are extracted) -- didn't do this myself since rewriting
`main`'s history isn't something to do without asking first.

## 2026-09-18 (evening, after seven rounds of real splat runs)

### Today's arc: crt0 stub -> confirmed resident code -> the overlay wall

Ran splat for real on Matt's machine (see "Blocked" below for why that
couldn't happen in the sandbox that built this scaffold). Getting it
running took a few config bugs (top-level segments must be `type: code`
with an explicit `subsegments` entry; splat resolves `base_path` relative
to the config file's own directory, not the invocation directory) -- all
fixed in `tools/splat.yaml`.

From there, seven rounds of narrowing (full history is in the comments at
the top of `tools/splat.yaml` -- worth reading in full, it's a genuinely
useful log of what worked and what didn't):

1. Scanning the whole ROM as one segment hung -- no `jr $ra` ever found.
2. Bounding it at the hang point produced a wall of raw `.word` output.
   Reading it by hand: real code is a **0x38-byte crt0 stub**
   (`0x80000400`-`0x80000438`) that clears BSS then does
   `jal 0x8009ED9C ; nop` -- strong additional confirmation the compiler is
   IDO/SN64-family, not GCC (this is the textbook idiom).
3. Probed the `jal` target (ROM `0x9F99C`) in a small window. Fully clean.
4-6. Widened progressively (8KB, then 128KB, then ~1.1MB) chasing a second
   hang, which turned out to be a misread: a `Select-String` count of 7,522
   "invalid instruction" hits at 128KB was wrongly written off as "small
   isolated data pockets" before confirming where output actually stopped.
   Checking the output directory directly (not just the progress bar) found
   it frozen writing a 0-byte file at the exact address those samples
   started at. **Confirmed real resident code: `0x9F99C`-`0xA449C` (~19KB),
   VRAM `0x8009ED9C`-`0x800A389C`.**
7. That confirmed 19KB block calls out to three further addresses
   (`func_801055CC`, `func_80105DB0`, `func_80105DD0`). Probed each
   individually rather than extending the linear scan again. All three
   turned out to be **texture/pixel-looking data**, not code, from byte
   zero -- not a partial corruption like every previous wall. This is the
   signature of an **overlay slot**: the real code the game expects at
   these RAM addresses gets DMA'd in at runtime from elsewhere in the ROM;
   what's sitting at the naive "linear" ROM offset is just whatever
   unrelated asset happens to occupy that byte range.

   (2026-09-27 note: re-examined above -- the unconditional, argument-free
   way `func_80105DB0` gets called from the very first instructions of the
   very first function the game runs doesn't fit a demand-loaded overlay
   well. Leading theory now is a wrong ROM-offset mapping past this point,
   not necessarily an overlay. Still open.)

**Where this leaves us**: this ROM has a real, non-trivial statically-
resident block (crt0 stub + ~19KB of dispatcher-style logic), which is more
than nothing, but code reachable beyond that isn't reliably at
`vram - 0x7FFFF400` in the ROM file. Further progress needs the actual
**overlay loader** -- almost certainly a DMA/file-load routine somewhere in
that confirmed 19KB block -- which is a Ghidra job (reading real code for
`osPiStartDma`-shaped calls), not something splat's config can find by
guessing more addresses.

## Done

- Confirmed the supplied ROM is v64 byte-swapped despite its `.n64`
  extension; normalized to big-endian `.z64`; header matches expected USA
  identification (`NBQE`) -- see `syms/rom_info.md`.
- Scaffolded the repo: `lib/` submodules for N64Recomp, N64ModernRuntime,
  RT64, RecompFrontend; `src/`, `include/`, `syms/`, `patches/`, `tools/`
  layout following VPW64Recomp/GGA-Recomp.
- Documented the known USA-ROM boot-timing race as a patch to expect --
  see `patches/README.md`.
- Got real, clean disassembly confirming the crt0 stub and ~19KB of
  resident dispatcher code, and confirmed (empirically, not by assumption)
  that this game uses an overlay system for code beyond that.

## Blocked / needs to happen elsewhere

The cloud sandbox this scaffold was built in has PyPI, npm, and crates.io
blocked by egress policy (confirmed genuine 403s). splat's real dependency
chain (`spimdisasm`, `rabbitizer`) couldn't be installed there. All the
actual disassembly work above happened on Matt's machine instead, driven
interactively from this session.

(2026-09-27 note: not true in every cloud sandbox -- see above.)

## Next steps, in order

1. Ghidra pass (N64 loader plugin) over the confirmed resident block
   (`0x9F99C`-`0xA449C`). Specifically look for DMA/file-read calls
   (`osPiStartDma` or equivalent) -- that's the overlay loader, and finding
   it is what unblocks reading any code beyond the resident block.
   (2026-09-27: `tools/OverlayScan6.java` is a Ghidra script written for
   this; not yet run. Worth trying headless in a cloud sandbox before
   assuming it needs Matt's machine.)
2. Once the loader is found, figure out its table format (what maps an
   overlay ID/address to a ROM source location) -- that turns "guess a jal
   target and hope" into "look it up properly."
3. Check whether the original 1998 BattleTanx N64 ROM is available, to
   byte-match shared engine/libultra functions against it (the trick both
   VPW64Recomp and GGA-Recomp used against their own sister titles) --
   still useful for identifying functions within the confirmed resident
   block, independent of the overlay question.
4. Locate the USA boot-race branch (`patches/README.md`) in the real
   symbol map and turn it into a real N64Recomp TOML patch entry.
5. Once the overlay mechanism is understood, start standing up the
   N64Recomp TOML config proper and get a first compile against
   N64ModernRuntime + RT64.
