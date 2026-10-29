"""Removes from the shareware DOOM1.WAD the screens a one-map build never shows (intermission statistics and the
episode map, help and credit pages, end pictures, border tiles), to make the E1M1 pack smaller.

usage: python3 -I trim_original_wad.py DOOM1.WAD OUT.wad

The result is a modified copy of copyrighted game data for the owner's own device: keep it outside the repository.
"""
import re
import struct
import sys

DROP = re.compile(r"^(WI[A-Z0-9]*|HELP\d?|CREDIT|INTERPIC|CWILV\d+|WIMAP\d|VICTORY2|ENDPIC|BOSSBACK|PFUB[12]|END[0-9]|BRDR_.*)$")

w = open(sys.argv[1], "rb").read()
_, num, off = struct.unpack("<4sII", w[:12])
lumps = []
for i in range(num):
    pos, size, name = struct.unpack("<II8s", w[off + 16 * i:off + 16 * i + 16])
    lumps.append((name.split(b"\0")[0].decode(), w[pos:pos + size]))
keep = [l for l in lumps if not DROP.match(l[0])]
body = b""
table = b""
for name, data in keep:
    table += struct.pack("<II8s", 12 + len(body), len(data), name.encode().ljust(8, b"\0"))
    body += data
open(sys.argv[2], "wb").write(struct.pack("<4sII", b"IWAD", len(keep), 12 + len(body)) + body + table)
print("dropped %d lumps (%d bytes), kept %d" % (len(lumps) - len(keep), sum(len(l[1]) for l in lumps) - sum(len(l[1]) for l in keep), len(keep)))
