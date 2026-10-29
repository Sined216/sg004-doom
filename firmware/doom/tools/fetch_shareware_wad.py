"""Fetches the original shareware DOOM1.WAD (v1.9) into a directory outside the repository and verifies it.

id Software released the shareware episode for free distribution; the archive is doom19s.zip from the idgames
mirrors (same recipe as the Arch Linux doom1-wad package: DOOMS_19.1 + DOOMS_19.2 is a self-extracting ZIP).
The WAD is game data and is never committed: keep OUTDIR outside the tree.

usage: python3 -I fetch_shareware_wad.py OUTDIR
"""
import hashlib
import io
import sys
import urllib.request
import zipfile

URL = "https://mirrors.lug.mtu.edu/idgames/idstuff/doom/doom19s.zip"
ZIP_SHA256 = "cacf0142b31ca1af00796b4a0339e07992ac5f21bc3f81e7532fe1b5e1b486e6"
WAD_SHA1 = "5b2e249b9c5133ec987b3ea77596381dc0d6bc1d"     # DOOM1.WAD 1.9, 4,196,020 bytes (doomwiki.org)

out = sys.argv[1]
data = urllib.request.urlopen(URL, timeout=120).read()
if hashlib.sha256(data).hexdigest() != ZIP_SHA256:
    raise SystemExit("doom19s.zip: unexpected SHA-256")
z = zipfile.ZipFile(io.BytesIO(data))
sfx = z.read("DOOMS_19.1") + z.read("DOOMS_19.2")
wad = zipfile.ZipFile(io.BytesIO(sfx)).read("DOOM1.WAD")
if hashlib.sha1(wad).hexdigest() != WAD_SHA1:
    raise SystemExit("DOOM1.WAD: unexpected SHA-1")
open(out.rstrip("/\\") + "/DOOM1.WAD", "wb").write(wad)
print("DOOM1.WAD ok (%d bytes) in %s" % (len(wad), out))
