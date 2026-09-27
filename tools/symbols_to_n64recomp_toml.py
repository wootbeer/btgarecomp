#!/usr/bin/env python3
"""
Convert a function/symbol list into the [[section]] TOML format N64Recomp's
symbol file expects (see BattleTanxGASyms/battletanxga.us.rev0.syms.toml and
https://github.com/N64Recomp/N64Recomp).

Accepts two input styles, autodetected per line:

1. CSV, as exported from Ghidra (Window -> Functions -> right-click ->
   Export, or a script dumping name/address/size):
       name,address,size
       func_80071000,0x80071000,0x3c

2. splat-style symbol_addrs.txt / undefined_funcs_auto.txt lines:
       func_80071000 = 0x80071000;
   (no size -- pass --default-size, or better, cross-reference against
   splat's own function-boundary output once a real split exists, since a
   guessed size is only a placeholder that N64Recomp will reject or misread
   function boundaries with.)

Usage:
    python3 symbols_to_n64recomp_toml.py functions.csv \
        --section-name .main --rom-base 0x1000 --vram-base 0x80071000 \
        --size 0x100000 -o BattleTanxGASyms/battletanxga.us.rev0.syms.toml
"""
import argparse
import re
import sys

LINE_RE = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0x[0-9A-Fa-f]+)\s*;?\s*$")


def parse_csv_line(line: str):
    parts = [p.strip() for p in line.split(",")]
    if len(parts) < 2:
        return None
    name = parts[0]
    if not name or name.lower() in ("name", "function name"):
        return None
    try:
        addr = int(parts[1], 16) if parts[1].lower().startswith("0x") else int(parts[1], 16)
    except ValueError:
        return None
    size = None
    if len(parts) >= 3 and parts[2]:
        try:
            size = int(parts[2], 16) if parts[2].lower().startswith("0x") else int(parts[2])
        except ValueError:
            size = None
    return name, addr, size


def parse_symbol_line(line: str):
    m = LINE_RE.match(line)
    if not m:
        return None
    name, addr = m.group(1), int(m.group(2), 16)
    return name, addr, None


def parse_input(path: str, default_size: int):
    funcs = []
    with open(path, "r") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#") or line.startswith("//"):
                continue
            entry = parse_csv_line(line) or parse_symbol_line(line)
            if entry is None:
                continue
            name, addr, size = entry
            funcs.append((name, addr, size if size is not None else default_size))
    funcs.sort(key=lambda t: t[1])
    return funcs


def infer_sizes(funcs, section_end: int):
    """When sizes are missing, use the gap to the next function as a stand-in.
    This is only ever a placeholder: it's wrong whenever there's padding,
    jump-table data, or an unlisted function between two entries, so treat
    the result as something to check against a real splat/Ghidra split
    before trusting it, not as ground truth."""
    out = []
    for i, (name, addr, size) in enumerate(funcs):
        if size:
            out.append((name, addr, size))
            continue
        nxt = funcs[i + 1][1] if i + 1 < len(funcs) else section_end
        out.append((name, addr, max(nxt - addr, 4)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="CSV (name,address,size) or splat-style 'name = 0xADDR;' list")
    ap.add_argument("-o", "--output", required=True, help="Path to write the N64Recomp syms.toml section to")
    ap.add_argument("--section-name", default=".main")
    ap.add_argument("--rom-base", type=lambda x: int(x, 16), required=True, help="ROM offset of the section, hex")
    ap.add_argument("--vram-base", type=lambda x: int(x, 16), required=True, help="VRAM address of the section, hex")
    ap.add_argument("--size", type=lambda x: int(x, 16), required=True, help="Section size in bytes, hex")
    ap.add_argument("--default-size", type=lambda x: int(x, 16), default=0,
                     help="Size to use for entries with none given (0 = infer from the gap to the next symbol)")
    ap.add_argument("--append", action="store_true", help="Append to an existing file instead of overwriting")
    args = ap.parse_args()

    funcs = parse_input(args.input, args.default_size)
    if not funcs:
        print("No function/symbol entries recognized in input.", file=sys.stderr)
        sys.exit(1)

    if args.default_size == 0:
        funcs = infer_sizes(funcs, args.vram_base + args.size)

    lines = []
    lines.append("[[section]]")
    lines.append(f'name="{args.section_name}"')
    lines.append(f"rom = {args.rom_base:#08x}")
    lines.append(f"vram = {args.vram_base:#08x}")
    lines.append(f"size = {args.size:#08x}")
    lines.append("")
    lines.append("functions = [")
    for name, addr, size in funcs:
        lines.append(f'    {{ name = "{name}", vram = {addr:#08x}, size = {size:#x} }},')
    lines.append("]")
    lines.append("")

    mode = "a" if args.append else "w"
    with open(args.output, mode) as f:
        f.write("\n".join(lines))

    print(f"Wrote {len(funcs)} function entries to {args.output}")
    if args.default_size == 0 and any(True for _, _, s in funcs if s):
        print("Sizes with no source data were inferred from gaps to the next symbol -- "
              "verify these against a real splat/Ghidra split before trusting them.")


if __name__ == "__main__":
    main()
