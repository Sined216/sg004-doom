#!/bin/sh
# Builds SG004_NORLOAD.bin: the WRITE build of the NOR loader (Doom data into the external flash, area above
# SG_NOR_MIN only). The read-only backup loader stays a separate build (build.sh) with no write code at all.
set -e
cd "$(dirname "$0")"
export PATH="/c/msys64/ucrt64/bin:$PATH"
mkdir -p build
CFLAGS="-mcpu=cortex-m4 -mthumb -Os -Wall -Wextra -ffunction-sections -fdata-sections -DSG_WRITE_BUILD"
# The owner's own backup CRCs (shown by the backup loader, or: python3 tools/crc_of_backup.py NOR.BIN):
#   HIST_CRC=0x....u CHIP_CRC=0x....u sh build_write.sh   (without them the build compares the History with itself)
[ -n "$HIST_CRC" ] && CFLAGS="$CFLAGS -DSG_HIST_CRC=$HIST_CRC"
[ -n "$CHIP_CRC" ] && CFLAGS="$CFLAGS -DSG_BACKUP_CHIP_CRC=$CHIP_CRC"
arm-none-eabi-gcc $CFLAGS --specs=nano.specs -T link.ld -Wl,--gc-sections -Wl,-Map=build/norload.map \
    startup.c clock.c lcd.c fault.c syscalls.c nor.c nor_write.c uf2.c usb.c msc.c vfat.c scsi.c crc32.c main_write.c \
    -o build/norload.elf
arm-none-eabi-objcopy -O binary build/norload.elf build/SG004_NORLOAD.bin
arm-none-eabi-size build/norload.elf
size=$(stat -c %s build/SG004_NORLOAD.bin)
echo "bin: $size of $((0x6A000)) bytes"
[ "$size" -le "$((0x6A000))" ] || { echo "image exceeds the app window"; rm -f build/SG004_NORLOAD.bin; exit 1; }

# Safety gate 1: no flash controller, option byte or status-register write anywhere
if grep -n -E "40022000|FLASH_KEYR|FLASH_CR|45670123|CDEF89AB|1FFFF800|nor_xfer\((0x01|0x31|0x11|0xC7|0x60|0xD8|0x52|0x42|0x44|0x48|0xB9)\)" \
        main_write.c nor.c nor_write.c uf2.c usb.c msc.c vfat.c scsi.c crc32.c clock.c lcd.c fault.c startup.c | grep -v "FLASH_ACR\|40022000u)"; then
    echo "SAFETY GATE: forbidden flash access found" >&2
    rm -f build/SG004_NORLOAD.bin
    exit 1
fi
# Safety gate 2: the flash opcodes exist in nor_write.c only, and only the six expected ones
others=$(grep -l -E "nor_xfer\(0x|CMD_ERASE|CMD_PROG|CMD_WREN" main_write.c nor.c uf2.c usb.c msc.c vfat.c scsi.c crc32.c clock.c lcd.c fault.c startup.c || true)
if [ -n "$others" ]; then
    echo "SAFETY GATE: opcodes outside nor_write.c: $others" >&2
    rm -f build/SG004_NORLOAD.bin
    exit 1
fi
cmds=$(grep -o -E "#define CMD_[A-Z0-9]+ +0x[0-9A-Fa-f]+" nor_write.c | awk '{print $3}' | sort | tr '\n' ' ')
if [ "$cmds" != "0x02 0x05 0x06 0x20 0x35 0x9F " ]; then
    echo "SAFETY GATE: unexpected opcode set in nor_write.c: $cmds" >&2
    rm -f build/SG004_NORLOAD.bin
    exit 1
fi
echo "safety gate ok: opcodes 0x02 0x05 0x06 0x20 0x35 0x9F in nor_write.c only, address guard in one place"
