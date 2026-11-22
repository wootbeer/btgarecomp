#!/usr/bin/env python3
"""Compares two BTGA_SYNCTEST files: the first frame where they differ,
and which 64 KB chunks of RDRAM (as N64 addresses)."""
import struct
import sys

CHUNKS = 192
RECORD = 16 + CHUNKS * 8


def frames(path):
    with open(path, "rb") as f:
        while True:
            rec = f.read(RECORD)
            if len(rec) < RECORD:
                return
            frame, mode, retraces = struct.unpack_from("<IIQ", rec)
            yield frame, (mode, retraces), struct.unpack_from("<%dQ" % CHUNKS, rec, 16)


def main():
    a, b = frames(sys.argv[1]), frames(sys.argv[2])
    count = 0
    reported = 0
    for (fa, ma, ha), (fb, mb, hb) in zip(a, b):
        count += 1
        diff = [i for i in range(CHUNKS) if ha[i] != hb[i]]
        if diff or ma != mb:
            print("frame %d: mode, retraces %s/%s, chunks differ: %s" % (fa, ma, mb,
                  " ".join("%08X" % (0x80000000 + i * 0x10000) for i in diff)))
            reported += 1
            if reported >= int(sys.argv[3] if len(sys.argv) > 3 else 5):
                return
    if reported == 0:
        print("%d frames, all the same" % count)


main()
