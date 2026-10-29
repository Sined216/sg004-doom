#!/bin/sh
# Builds SG004_NORBACKUP.bin for the FNIRSI bootloader (app base 0x08005800, at most 0x6A000 bytes).
# Read-only build: the sources contain no flash program/erase/write-enable code. build.sh checks that.
set -e
cd "$(dirname "$0")"
export PATH="/c/msys64/ucrt64/bin:$PATH"
mkdir -p build
CFLAGS="-mcpu=cortex-m4 -mthumb -Os -Wall -Wextra -ffunction-sections -fdata-sections"
arm-none-eabi-gcc $CFLAGS --specs=nano.specs -T link.ld -Wl,--gc-sections -Wl,-Map=build/loader.map \
    startup.c clock.c lcd.c fault.c syscalls.c nor.c usb.c msc.c vfat.c scsi.c crc32.c main.c -o build/loader.elf
arm-none-eabi-objcopy -O binary build/loader.elf build/SG004_NORBACKUP.bin
arm-none-eabi-size build/loader.elf
size=$(stat -c %s build/SG004_NORBACKUP.bin)
max=$((0x6A000))
echo "bin: $size of $max bytes"
[ "$size" -le "$max" ] || { echo "image exceeds the app window"; rm -f build/SG004_NORBACKUP.bin; exit 1; }

# Safety gate: no internal-flash controller or external-flash write/erase opcodes may be present in the sources.
if grep -n -E "40022000|FLASH_KEYR|FLASH_CR|45670123|CDEF89AB|xfer\((0x02|0x20|0x06|0xC7|0x60|0xD8|0x52|0x01)\)" \
        main.c nor.c usb.c msc.c vfat.c scsi.c crc32.c clock.c lcd.c | grep -v "FLASH_ACR\|40022000u)" ; then
    echo "SAFETY GATE: flash write/erase code found in the sources" >&2
    rm -f build/SG004_NORBACKUP.bin
    exit 1
fi
echo "safety gate ok: no flash write/erase code in the sources"
