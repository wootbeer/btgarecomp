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

The `NBQE` code and region byte line up with how No-Intro/Redump catalog
"BattleTanx - Global Assault (USA)", which is a reasonable sanity check that
this is a standard, unmodified USA dump -- worth a final CRC cross-check
against a Redump/No-Intro DAT once we're online from a machine that can
reach those databases.

Rough, dependency-free function-count sanity check
(`tools/rough_function_scan.py`, see that file's caveats):
~1,900 `jr $ra` epilogue markers across the ROM, averaging ~353 bytes apart.
That's the right order of magnitude for a game this size and roughly in
line with GGA-Recomp's reported ~2,639 functions for a comparably-scoped
title -- not a real function count, just a plausibility check that this
ROM looks like ordinary N64 code and not something pre-compressed or
unusually structured.
