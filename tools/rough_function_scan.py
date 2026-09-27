#!/usr/bin/env python3
"""
Rough, dependency-free stand-in for splat's function-boundary detection.

This is NOT a replacement for splat + spimdisasm/rabbitizer -- it exists only
because this project was scaffolded in a sandbox where PyPI/npm/crates.io
were blocked by egress policy, so the real toolchain (splat's `[mips]` extra)
couldn't be installed. It gives a ballpark "how many functions are in here"
sanity check by counting `jr $ra` epilogue markers (MIPS opcode 0x03E00008),
which is the same basic signal splat/asm-differ-style tools use as one input
among several, without any of the surrounding validation (delay-slot checks,
alignment, code-vs-data discrimination) that makes it reliable.

Once splat can actually be run (locally, with normal internet access -- see
the top-level README), throw this away in favor of its real output.
"""
import struct
import sys

JR_RA = 0x03E00008


def scan(data: bytes) -> list[int]:
    positions = []
    for off in range(0, len(data) - 3, 4):
        word = struct.unpack(">I", data[off:off + 4])[0]
        if word == JR_RA:
            positions.append(off)
    return positions


def main() -> None:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <normalized .z64 rom>")
        sys.exit(1)

    with open(sys.argv[1], "rb") as f:
        data = f.read()

    positions = scan(data)
    print(f"ROM size: {len(data)} bytes ({len(data)//4} words)")
    print(f"jr $ra occurrences (candidate function ends): {len(positions)}")
    if len(positions) > 1:
        gaps = [positions[i + 1] - positions[i] for i in range(len(positions) - 1)]
        avg = sum(gaps) / len(gaps)
        print(f"average spacing between epilogues: {avg:.1f} bytes")
    print()
    print("This count includes false positives from data that happens to look")
    print("like `jr $ra` and says nothing about overlays vs. resident code --")
    print("treat it only as an order-of-magnitude sanity check.")


if __name__ == "__main__":
    main()
