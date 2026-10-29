"""
Builds a cut-down WAD from Freedoom Phase 1 for the SG-004A Doom port.

Keeps E1M1 geometry, remaps its wall textures and flats to the few most used ones and thins the THINGS
list, so the converter only has to pack a small set of textures, flats and sprites.

usage: python3 -I trim_wad.py freedoom-0.13.0.zip out.wad [--tex N] [--flats N] [--map E1M1]
"""
import argparse
import collections
import re
import struct
import zipfile

KEEP_THINGS = {
    1,                      # player 1 start
    3004, 3001,             # zombieman, imp
    2035,                   # barrel
    2011, 2048, 2018,       # stimpack, bullets box, armor
    2007,                   # clip
}


# Lumps that a one-map game never shows: intermission screens, help/credit pages, finale pictures (TITLEPIC stays: it is the background of the title and the menu)
DROP_RE = re.compile(r"^(WI[A-Z0-9]*|HELP\d?|HELP1|CREDIT|INTERPIC|CWILV\d+|WIMAP\d|VICTORY2|ENDPIC|BOSSBACK|PFUB[12]|END[0-9]|BRDR_.*)$")


def read_wad(data):
    _, num, off = struct.unpack("<4sII", data[:12])
    lumps = []
    for i in range(num):
        pos, size, name = struct.unpack("<II8s", data[off + 16 * i:off + 16 * i + 16])
        lumps.append([name.split(b"\0")[0].decode(), data[pos:pos + size]])
    return lumps


def write_wad(lumps):
    body = b""
    table = b""
    for name, data in lumps:
        table += struct.pack("<II8s", 12 + len(body), len(data), name.encode().ljust(8, b"\0"))
        body += data
    return struct.pack("<4sII", b"IWAD", len(lumps), 12 + len(body)) + body + table


def name8(b):
    return b.split(b"\0")[0].decode()


def pad8(s):
    return s.encode().ljust(8, b"\0")


def remap_sidedefs(data, keep_n):
    use = collections.Counter()
    for k in range(0, len(data), 30):
        for o in (4, 12, 20):
            t = name8(data[k + o:k + o + 8])
            if t and t != "-":
                use[t] += 1
    keep = [t for t, _ in use.most_common(keep_n)]
    out = bytearray(data)
    for k in range(0, len(out), 30):
        for o in (4, 12, 20):
            t = name8(out[k + o:k + o + 8])
            if t and t != "-" and t not in keep:
                out[k + o:k + o + 8] = pad8(keep[sum(t.encode()) % len(keep)])
    return bytes(out), keep


def remap_sectors(data, keep_n):
    use = collections.Counter()
    for k in range(0, len(data), 26):
        for o in (4, 12):
            use[name8(data[k + o:k + o + 8])] += 1
    keep = [f for f, _ in use.most_common() if f != "F_SKY1"][:keep_n] + ["F_SKY1"]
    out = bytearray(data)
    for k in range(0, len(out), 26):
        for o in (4, 12):
            f = name8(out[k + o:k + o + 8])
            if f not in keep:
                out[k + o:k + o + 8] = pad8(keep[sum(f.encode()) % (len(keep) - 1)])
    return bytes(out), keep


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("zip")
    ap.add_argument("out")
    ap.add_argument("--tex", type=int, default=12)
    ap.add_argument("--flats", type=int, default=6)
    ap.add_argument("--map", default="E1M1")
    ap.add_argument("--keep-ui", action="store_true", help="do not drop intermission/help/title lumps")
    a = ap.parse_args()

    z = zipfile.ZipFile(a.zip)
    lumps = read_wad(z.read([n for n in z.namelist() if n.endswith("freedoom1.wad")][0]))
    mi = next(i for i, l in enumerate(lumps) if l[0] == a.map)
    ml = {lumps[mi + k][0]: mi + k for k in range(1, 11)}

    lumps[ml["SIDEDEFS"]][1], tex = remap_sidedefs(lumps[ml["SIDEDEFS"]][1], a.tex)
    lumps[ml["SECTORS"]][1], flats = remap_sectors(lumps[ml["SECTORS"]][1], a.flats)
    th = lumps[ml["THINGS"]][1]
    lumps[ml["THINGS"]][1] = b"".join(th[k:k + 10] for k in range(0, len(th), 10)
                                      if struct.unpack("<H", th[k + 6:k + 8])[0] in KEEP_THINGS)
    if not a.keep_ui:
        before = sum(len(l[1]) for l in lumps)
        lumps = [l for l in lumps if not DROP_RE.match(l[0])]
        print("dropped UI lumps: %d bytes" % (before - sum(len(l[1]) for l in lumps)))
    open(a.out, "wb").write(write_wad(lumps))
    print("textures kept:", ", ".join(tex))
    print("flats kept:", ", ".join(flats))
    print("things kept:", len(lumps[ml["THINGS"]][1]) // 10, "of", len(th) // 10)


main()
