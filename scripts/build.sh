#!/bin/sh
# Builds everything needed to run Doom on the FNIRSI SG-004A from the sources in this repository.
#
#   scripts/build.sh freedoom [--out DIR] [--backup NOR.BIN]
#   scripts/build.sh original --wad DOOM1.WAD [--out DIR] [--backup NOR.BIN]    (or without --wad: download the shareware WAD)
#
# Result in DIR (default ./out):
#   SG004_NORBACKUP.bin  read-only loader: shows the external flash as a USB drive, copy NOR.BIN from it (your backup)
#   SG004_NORLOAD.bin    loader that writes the game data into the external flash (sectors 56..255 only)
#   doom.uf2             the game data for that loader (copy it onto the loader's drive)
#   SG004_DOOM.bin       the game, with the first part of the data; copy it onto the BOOTLOADER drive
#
# --backup NOR.BIN: your own backup made with SG004_NORBACKUP.bin. Its checksums are built into SG004_NORLOAD.bin, which
# then refuses to write unless the stock History still reads exactly like the backup, and proves the way back after
# "erase Doom area". Without it the loader only checks that the History reads at the end as it did at the start.
#
# Needs: sh, git, python 3, clang, arm-none-eabi-gcc/ld/objcopy (GNU Arm Embedded) and internet for the first run.
# Data derived from DOOM1.WAD is copyrighted: keep out/ for your own device and never publish it.
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DOOM=$ROOT/firmware/doom
LOAD=$ROOT/firmware/doomload

WHAT=${1:?usage: scripts/build.sh freedoom|original [--wad FILE] [--out DIR] [--backup NOR.BIN]}
shift
OUT=$PWD/out
WAD=""
BACKUP=""
while [ $# -gt 0 ]; do
    case $1 in
        --out) OUT=$2; shift 2;;
        --wad) WAD=$2; shift 2;;
        --backup) BACKUP=$2; shift 2;;
        *) echo "unknown option $1" >&2; exit 2;;
    esac
done
case $WHAT in freedoom|original) ;; *) echo "first argument: freedoom or original" >&2; exit 2;; esac

PY=""
for p in ${PYTHON:-} python3 python; do
    [ -n "$p" ] && "$p" -c 'import sys; sys.exit(sys.version_info[0] < 3)' 2>/dev/null && { PY=$p; break; }
done
[ -n "$PY" ] || { echo "python 3 not found (set PYTHON=)" >&2; exit 1; }

WORK=$OUT/work
mkdir -p "$WORK" "$OUT"

# ---- 1. the WAD, cut down to what one map needs
if [ "$WHAT" = freedoom ]; then
    ZIP=$WORK/freedoom-0.13.0.zip
    if [ ! -f "$ZIP" ]; then
        echo "downloading Freedoom 0.13.0"
        "$PY" -I - "$ZIP" <<'EOF'
import hashlib, sys, urllib.request
URL = "https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip"
SHA256 = "3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59"
data = urllib.request.urlopen(URL, timeout=300).read()
if hashlib.sha256(data).hexdigest() != SHA256:
    raise SystemExit("freedoom-0.13.0.zip: unexpected SHA-256")
open(sys.argv[1], "wb").write(data)
EOF
    fi
    # 28 wall textures and 12 flats: what fits next to the rest of E1M1 in the 1,073,152 byte data area
    "$PY" -I "$DOOM/tools/trim_wad.py" "$ZIP" "$WORK/trimmed.wad" --tex 28 --flats 12
else
    if [ -z "$WAD" ]; then
        mkdir -p "$WORK/wad"
        "$PY" -I "$DOOM/tools/fetch_shareware_wad.py" "$WORK/wad"
        WAD=$WORK/wad/DOOM1.WAD
    fi
    "$PY" -I "$DOOM/game/tools/trim_original_wad.py" "$WAD" "$WORK/trimmed.wad"
fi

# ---- 2. converter (needs the pinned doomgeneric source for its tables) and the two halves of the data
sh "$DOOM/dmc100/oracle/fetch.sh"
(cd "$DOOM/dmc100" && "$PY" -I tools/conv/__main__.py --wad "$WORK/trimmed.wad" --gen "$WORK/gen" \
    --out "$WORK/packs" --capacity 1073152 --packs E1M1 all)
"$PY" -I "$DOOM/game/tools/make_data.py" "$WORK/packs/E1M1/DOOM.DAT" "$WORK/data"

# ---- 3. the game and its image
GEN="$WORK/gen" OUT="$WORK/game" sh "$DOOM/game/build_game.sh"
"$PY" -I "$DOOM/game/tools/make_game_image.py" "$WORK/game/SG004_DOOM_GAME.bin" "$WORK/data/doom.internal.bin" "$OUT/SG004_DOOM.bin"
cp "$WORK/data/doom.uf2" "$OUT/doom.uf2"

# ---- 4. the loaders
(cd "$LOAD" && sh build.sh) && cp "$LOAD/build/SG004_NORBACKUP.bin" "$OUT/"
if [ -n "$BACKUP" ]; then
    CRCS=$("$PY" -I - "$BACKUP" "$DOOM/game/sg_layout.h" <<'EOF'
import re, sys, zlib
b = open(sys.argv[1], "rb").read()
if len(b) != 1 << 20:
    raise SystemExit("backup must be exactly 1 MiB (NOR.BIN from SG004_NORBACKUP.bin)")
m = int(re.search(r"#define\s+SG_NOR_MIN\s+(0x[0-9A-Fa-f]+)", open(sys.argv[2]).read()).group(1), 16)
print("HIST_CRC=0x%08Xu CHIP_CRC=0x%08Xu" % (zlib.crc32(b[:m]), zlib.crc32(b)))
EOF
)
    echo "backup checksums: $CRCS"
    eval "export $CRCS"
fi
(cd "$LOAD" && sh build_write.sh) && cp "$LOAD/build/SG004_NORLOAD.bin" "$OUT/"

echo
echo "done. Files in $OUT:"
ls -l "$OUT"/*.bin "$OUT"/*.uf2
