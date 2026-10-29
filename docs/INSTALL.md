# Safety and rollback

The steps are in the [README](../README.md#install). This page says what the tools write, where, and how to go back.

## What is written

* **Internal flash:** only the application window `0x08005800`-`0x0806F800`, by the stock bootloader, when you copy
  `SG004_DOOM.bin` or `SG004_NORLOAD.bin`. The bootloader itself, the option bytes and the I²C EEPROM (calibration data) are
  never written. The game code contains no flash write or erase code (checked by `grep` in the build).
* **External SPI flash (W25Q80, 1 MB):** only by `SG004_NORLOAD.bin`, over USB, only from `0x38000` up (sector 56). The stock
  History uses the first 31 sectors; sectors 31-55 are left as headroom for it. One function checks the address of every
  erase and program. The loader refuses to write on any chip but a W25Q80 (JEDEC `EF 40 14`) or when block-protect bits are set,
  reads every page back after programming, and checks at the end that the History reads as it did at the start.
  The status register is never written.
* **`SG004_NORBACKUP.bin`** contains no write code at all and refuses all writes.

The History can still grow only into sectors 31-55 while Doom's data is installed. What the stock firmware does when it
needs more than that has not been tested.

## The loader and your backup

`scripts/build.sh ... --backup NOR.BIN` builds `SG004_NORLOAD.bin` with the checksums of **your** backup. It then refuses to
write unless the History still reads exactly like the backup, and the erase mode can prove that the chip is back to the
backup state. The release file has no such checksums: it only compares the History with itself before and after, and refuses
an area that is neither blank nor Doom data. Building it yourself with your backup is stricter and recommended.

## Back to stock

* **Firmware:** copy FNIRSI's stock `.bin` to the `BOOTLOADER` drive. It does not depend on anything here.
* **External flash as it was:** power on `SG004_NORLOAD.bin` with **FN held**, then hold **OK** for 2 s. It erases sectors
  56-255 only. Built with your backup, it ends with `= backup: back to stock` if the whole chip has the backup's CRC.
  Otherwise `NOR.BIN` from step 1 of the install is the reference.
