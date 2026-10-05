# ROM identification

Source dump: `BattleTanx - Global Assault (USA).n64` (despite the `.n64`
extension, this is actually **v64 / byte-swapped** data -- verified by magic
bytes, not by filename). Normalize with `tools/normalize_rom.py` before
feeding anything to splat or N64Recomp, which expect big-endian `.z64`.

| Field | Value |
|---|---|
| Internal name | `BATTLETANXGA` |
| Game code | `NBQE` (cart id `BQ`, region `E` = USA, version 0 = rev 1.0) |
| Entry point (PC) | `0x80071000` |
| CRC1 | `0x75a4e247` |
| CRC2 | `0x6008963d` |
| Size | 8,388,608 bytes (8 MiB) |

**ROM-to-RAM mapping (added 2026-09-27, round 10 -- read this before computing
any ROM offset from a VRAM address, or vice versa):** this ROM does **not**
use the common "IPL3 always loads to VRAM 0x80000400" convention. Its
correct header displacement is `entry point - 0x1000` = **`0x80070000`**,
i.e. `rom_offset = vram - 0x80070000` (equivalently, ROM offset `0x1000`,
right after IPL3, maps directly to VRAM `0x80071000` -- this ROM's own
entry point, exactly). Confirmed three independent ways (full detail in
STATUS.md's round-10 entry): the crt0 stub's BSS-clear loop and `jal
0x8009ED9C` decode cleanly either way (a `jal`'s target doesn't depend on
which theory is right), but under this header that target's own first call
resolves to a real `osInitialize`, and a later call resolves to a
byte-exact `__osDisableInt` -- neither happens under the previously-assumed
`vram - 0x7FFFF400` mapping, which instead points at unrelated garbage for
both. Every ROM-offset-specific finding from before round 10 used the wrong
formula and needs to be re-derived; see `tools/splat.yaml` for the
corrected segment config.

The `NBQE` code and region byte line up with how No-Intro/Redump catalog
"BattleTanx - Global Assault (USA)", which is a reasonable sanity check that
this is a standard, unmodified USA dump -- worth a final CRC cross-check
against a Redump/No-Intro DAT once we're online from a machine that can
reach those databases.

Rough, dependency-free function-count sanity check
(`tools/rough_function_scan.py`, see that file's caveats):
~1,900 `jr $ra` epilogue markers across the ROM, averaging ~353 bytes apart.
That's the right order of magnitude for a game this size -- not a real
function count, just a plausibility check that this
ROM looks like ordinary N64 code and not something pre-compressed or
unusually structured.
