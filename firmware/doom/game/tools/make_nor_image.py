"""Builds a NOR image for the SG-004A Doom firmware / the emulator test: the game data region as sg_fat.c expects it.

  region at BASE:  "SG04DOOM" (8)  u32 version = 1  u32 size of DOOM.DAT  zeros up to 4096,  then DOOM.DAT

usage: python3 -I make_nor_image.py DOOM.DAT OUT.img [--base 0x200000] [--size 4194304]

The image is only a file on the PC; this script does not touch any device.
"""
import argparse
import struct

ap = argparse.ArgumentParser()
ap.add_argument("doom_dat")
ap.add_argument("out")
ap.add_argument("--base", type=lambda s: int(s, 0), default=0x200000)
ap.add_argument("--size", type=lambda s: int(s, 0), default=4 * 1024 * 1024)
a = ap.parse_args()

data = open(a.doom_dat, "rb").read()
hdr = b"SG04DOOM" + struct.pack("<II", 1, len(data))
hdr = hdr.ljust(4096, b"\0")
end = a.base + len(hdr) + len(data)
if end > a.size:
    raise SystemExit("image does not fit: needs %d bytes, chip has %d" % (end, a.size))
img = bytearray(b"\xff" * a.size)
img[a.base:a.base + len(hdr)] = hdr
img[a.base + len(hdr):end] = data
open(a.out, "wb").write(img)
print("wrote %s: %d bytes, DOOM.DAT %d bytes at 0x%X" % (a.out, len(img), len(data), a.base + len(hdr)))
