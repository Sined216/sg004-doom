"""Cuts the converter's DOOM.DAT into the two halves of the SG-004A layout (see ../sg_layout.h) and writes:

  OUT/doom.internal.bin  the front part, goes into the game .bin at SG_INT_BASE (make_game_image.py)
  OUT/doom.nor.part      header block + the rest, as it lies in the NOR from SG_NOR_MIN on
  OUT/doom.uf2           the same as 512-byte UF2 blocks (256-byte payload, target = NOR offset) for the NOR loader
  OUT/doom.nor.img       a 1 MB NOR image for the emulator: BACKUP's History part + the data (never flashed anywhere)
  OUT/manifest.txt       sizes and CRC-32s

usage: python3 -I make_data.py DOOM.DAT OUTDIR [--backup NOR.BIN]
The constants are read from sg_layout.h so that there is one place to change them.
"""
import argparse
import os
import re
import struct
import sys
import zlib

here = os.path.dirname(os.path.abspath(__file__))


def layout():
    t = open(os.path.join(here, "..", "sg_layout.h"), encoding="utf-8").read()
    v = {}
    for name in ("SG_INT_BASE", "SG_INT_CLUSTERS", "SG_NOR_SIZE", "SG_NOR_MIN", "SG_HDR_BYTES", "SG_VERSION"):
        m = re.search(r"#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)u?" % name, t)
        v[name] = int(m.group(1), 0)
    v["SG_INT_BYTES"] = v["SG_INT_CLUSTERS"] * 4096
    v["SG_MAGIC"] = re.search(r'#define\s+SG_MAGIC\s+"([^"]+)"', t).group(1).encode()
    return v


UF2_MAGIC0, UF2_MAGIC1, UF2_MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
UF2_FLAG_FAMILY = 0x00002000
UF2_FAMILY = 0x34304753        # "SG04" little endian


def uf2_block(addr, payload, n, total):
    assert len(payload) == 256
    blk = struct.pack("<8I", UF2_MAGIC0, UF2_MAGIC1, UF2_FLAG_FAMILY, addr, 256, n, total, UF2_FAMILY)
    blk += payload + b"\0" * (476 - 256)
    blk += struct.pack("<I", UF2_MAGIC_END)
    assert len(blk) == 512
    return blk


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("doom_dat")
    ap.add_argument("outdir")
    ap.add_argument("--backup", default=os.path.join(here, "..", "..", "..", "..", "NORBACKUP", "NOR.BIN"))
    a = ap.parse_args()
    L = layout()
    dat = open(a.doom_dat, "rb").read()
    size = len(dat)
    if dat[:8] != b"DMCDOOM\0" and not dat[:7] == b"DMCDOOM":
        print("warning: DOOM.DAT does not start with the DMC magic (%r)" % dat[:8])
    int_len = (size & ~4095) if size < L["SG_INT_BYTES"] else L["SG_INT_BYTES"]
    internal = dat[:int_len]
    rest = dat[int_len:]
    room = L["SG_NOR_SIZE"] - L["SG_NOR_MIN"] - L["SG_HDR_BYTES"]
    if len(rest) > room:
        sys.exit("NOR part is %d bytes, only %d fit above SG_NOR_MIN: lower SG_NOR_MIN or shrink the pack" % (len(rest), room))
    uuid = dat[20:36]
    hdr = L["SG_MAGIC"] + struct.pack("<III", L["SG_VERSION"], size, int_len) + uuid
    hdr = hdr.ljust(L["SG_HDR_BYTES"], b"\xff")
    nor_part = hdr + rest
    padded = nor_part + b"\xff" * (-len(nor_part) % 256)
    os.makedirs(a.outdir, exist_ok=True)
    open(os.path.join(a.outdir, "doom.internal.bin"), "wb").write(internal)
    open(os.path.join(a.outdir, "doom.nor.part"), "wb").write(nor_part)
    total = len(padded) // 256
    with open(os.path.join(a.outdir, "doom.uf2"), "wb") as f:
        for i in range(total):
            f.write(uf2_block(L["SG_NOR_MIN"] + 256 * i, padded[256 * i:256 * i + 256], i, total))
    img = bytearray(b"\xff" * L["SG_NOR_SIZE"])
    if os.path.exists(a.backup):
        bk = open(a.backup, "rb").read()
        img[:L["SG_NOR_MIN"]] = bk[:L["SG_NOR_MIN"]]
    img[L["SG_NOR_MIN"]:L["SG_NOR_MIN"] + len(padded)] = padded
    open(os.path.join(a.outdir, "doom.nor.img"), "wb").write(img)
    crc = lambda b: "%08X" % (zlib.crc32(b) & 0xFFFFFFFF)
    man = [
        "DOOM.DAT      %d bytes  crc32 %s" % (size, crc(dat)),
        "internal part %d bytes (%d clusters) at 0x%08X..0x%08X  crc32 %s" % (
            len(internal), len(internal) // 4096, L["SG_INT_BASE"], L["SG_INT_BASE"] + len(internal), crc(internal)),
        "NOR part      %d bytes + %d header at NOR 0x%X..0x%X (limit 0x%X)  crc32 %s" % (
            len(rest), L["SG_HDR_BYTES"], L["SG_NOR_MIN"], L["SG_NOR_MIN"] + len(padded), L["SG_NOR_SIZE"], crc(padded)),
        "UF2           %d blocks, %d bytes" % (total, total * 512),
        "image uuid    %s" % uuid.hex(),
    ]
    open(os.path.join(a.outdir, "manifest.txt"), "w").write("\n".join(man) + "\n")
    print("\n".join(man))


main()
