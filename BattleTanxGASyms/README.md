`battletanxga.us.rev0.syms.toml` is N64Recomp's symbol file for this game: the
function boundaries (vram, size and name) for the first, automatically loaded
MB of the ROM, which holds all of the game's code. `battletanxga.us.rev0.toml`
in the repo root points N64Recomp at it.

It started as a splat/spimdisasm scan seeded with n64sym's libultra and audio
library matches (`tools/symbols_to_n64recomp_toml.py` turns a symbol list into
`[[section]]` blocks), and has been corrected by hand since wherever a merged or
mis-sized function turned up -- see the file's own header and STATUS.md for the
history. `tools/scan_missing_functions.py` checks for function pointers that
still land inside another function.
