"""Parses the volume image like a host would (boot sector, FAT16 chains, root directory)
and checks that NOR.BIN is the fake NOR byte for byte and INFO.TXT carries the right CRC32.
usage: python3 -I check_fat.py image nor.bin"""
import struct
import sys
import zlib

img = open(sys.argv[1], "rb").read()
nor = open(sys.argv[2], "rb").read()

bps, spc, rsv, nfats, rootent = struct.unpack_from("<HBHBH", img, 11)
tot16, media, fatsz = struct.unpack_from("<HBH", img, 19)
tot32 = struct.unpack_from("<I", img, 32)[0]
total = tot16 or tot32
assert bps == 512 and spc == 1 and nfats == 2 and media == 0xF8
assert img[510:512] == b"\x55\xaa"
assert len(img) == total * 512, (len(img), total)

root_start = rsv + nfats * fatsz
root_secs = rootent * 32 // bps
data_start = root_start + root_secs
clusters = total - data_start
assert 4085 <= clusters < 65525, "not FAT16 by cluster count: %d" % clusters
assert img[54:62] == b"FAT16   "

fat1 = img[rsv * 512:(rsv + fatsz) * 512]
fat2 = img[(rsv + fatsz) * 512:(rsv + 2 * fatsz) * 512]
assert fat1 == fat2
fat = struct.unpack("<%dH" % (len(fat1) // 2), fat1)
assert fat[0] == 0xFFF8 and fat[1] == 0xFFFF


def chain(c):
    out = []
    while c < 0xFFF8:
        out.append(c)
        assert len(out) <= clusters
        c = fat[c]
    return out


def read_file(start, size):
    data = b"".join(img[(data_start + c - 2) * 512:(data_start + c - 1) * 512] for c in chain(start))
    assert len(data) >= size
    return data[:size]


root = img[root_start * 512:data_start * 512]
files = {}
for i in range(0, len(root), 32):
    e = root[i:i + 32]
    if e[0] in (0, 0xE5) or e[11] & 0x08:
        continue
    name = (e[0:8].decode().strip() + "." + e[8:11].decode().strip()).strip(".")
    cl, size = struct.unpack_from("<H", e, 26)[0], struct.unpack_from("<I", e, 28)[0]
    files[name] = (cl, size, e[11])
assert set(files) == {"NOR.BIN", "INFO.TXT"}, files
assert files["NOR.BIN"][2] & 1, "NOR.BIN must be read-only"

nb = read_file(files["NOR.BIN"][0], files["NOR.BIN"][1])
assert nb == nor, "NOR.BIN differs from the fake NOR"
info = read_file(files["INFO.TXT"][0], files["INFO.TXT"][1]).decode()
crc = "%08X" % (zlib.crc32(nor) & 0xFFFFFFFF)
assert ("CRC32 " + crc) in info, (info, crc)
assert ("SIZE %d" % len(nor)) in info
# every cluster after INFO.TXT is free
last = chain(files["INFO.TXT"][0])[-1]
assert all(v == 0 for v in fat[last + 1:clusters + 2])
print("check_fat ok: %d clusters, NOR.BIN %d B == fake NOR, crc32 %s" % (clusters, len(nb), crc))
