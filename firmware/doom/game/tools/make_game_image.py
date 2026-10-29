"""Joins the game code and the internal half of the data into one .bin for the FNIRSI bootloader.

usage: python3 -I make_game_image.py CODE.bin INTERNAL.bin OUT.bin

The result starts at 0x08005800 (like every app .bin): code, 0xFF padding up to SG_INT_BASE, the internal data, and
nothing beyond 0x0806F000 (the window of the bootloader ends at 0x0806F800 and the page at 0x08070000 stays erased).
"""
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
t = open(os.path.join(here, "..", "sg_layout.h"), encoding="utf-8").read()
int_base = int(re.search(r"#define\s+SG_INT_BASE\s+(0x[0-9A-Fa-f]+)", t).group(1), 16)
BASE, WINDOW_END = 0x08005800, 0x0806F800

code = open(sys.argv[1], "rb").read()
internal = open(sys.argv[2], "rb").read()
if BASE + len(code) > int_base:
    sys.exit("code ends at 0x%08X, past the data base 0x%08X" % (BASE + len(code), int_base))
end = int_base + len(internal)
if end > WINDOW_END:
    sys.exit("image would end at 0x%08X, beyond the window end 0x%08X" % (end, WINDOW_END))
img = bytearray(b"\xff" * (end - BASE))
img[:len(code)] = code
img[int_base - BASE:] = internal
open(sys.argv[3], "wb").write(img)
print("wrote %s: %d bytes, code %d bytes, data 0x%08X..0x%08X" % (sys.argv[3], len(img), len(code), int_base, end))
