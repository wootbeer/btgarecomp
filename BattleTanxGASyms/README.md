This is where `battletanxga.us.rev0.syms.toml` (N64Recomp's symbol file for
this game, in the format `bdragoncore/battle-tanx-recomp`'s
`BattleTanxSyms/battletanx.us.rev0.syms.toml` uses) will go once there's a
real function boundary list to put in it.

`tools/symbols_to_n64recomp_toml.py` generates this file's `[[section]]`
blocks from a Ghidra export or a splat-style symbol list — see
`PROGRESS.md` and `STATUS.md` for where that data is expected to come from.
