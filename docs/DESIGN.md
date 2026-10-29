# Design notes

Facts marked *(measured)* were checked on a real SG-004A with stock firmware `V3.0_20230825`; the rest comes from the
datasheets and from reading the code.

## Hardware used

* MCU Artery AT32F403AVGT7 (Cortex-M4F), 96 KB SRAM as configured by the stock firmware, internal flash in two banks of
  512 KB. Clock raised from the stock 72 MHz to 240 MHz (HSE 8 MHz, PLL); a 2 s POWER hold is handled in SysTick so
  that the device can always be switched off.
* Display: ST7789, 320x240, on the FSMC bus, written directly (polled, column windows). No DMA yet.
* Keys: nine GPIO inputs, active low, sampled and debounced at 1 kHz in SysTick, events latched and posted to the engine from
  `I_StartTic` (`firmware/doom/game/sg_input.c`).
* External flash: W25Q80 (JEDEC `EF 40 14`, 1 MB, 256 sectors of 4 KB) on SPI2 (CS PB12, SCK PB13, MISO PB14, MOSI PB15,
  mode 3). The stock firmware keeps its measurement History in the first 31 sectors *(measured)*.

## The stock bootloader

* The application is loaded at `0x08005800`; the FNIRSI USB mass-storage bootloader below it exposes a `BOOTLOADER` drive
  and programs a copied `.bin` *(measured)*.
* It does **not** load the main stack pointer from the image: `Reset_Handler` must set MSP itself (`game/startup.c`),
  otherwise the first stack use faults *(measured)*.
* It programs only the first 512 KB bank, up to `0x0807FFFF`; a `.bin` that reaches further is written only up to that
  limit, and the second bank stays erased *(measured)*. Our window is deliberately smaller: `0x08005800`-`0x0806F800`.
  The page at `0x08070000` (a calibration table of the stock firmware) stays erased and untouched.

## Memory

RAM (96 KB): engine and platform `.bss`, then a frame/record-cache area of 25,504 bytes handed to the DMC100 renderer
(it works in strips), then an 8 KB stack at the top (`game/link.ld`).

Internal flash: code at `0x08005800` (about 125 KB), data from `0x08030000`.

## Why the data is split

The engine reads its data pack (`DOOM.DAT`, about 1.0-1.06 MB for E1M1) through a `nor_read` interface and does not execute
anything from it. The internal window cannot hold it (the bootloader writes only below `0x08080000`), and the external
flash is 1 MB with the stock History in its lower part. So the pack is cut in two (`firmware/doom/game/sg_layout.h`
is the single source of the numbers):

* the first 258,048 bytes sit in the internal flash behind the code, in the same `.bin` the bootloader writes;
* the rest sits in the external flash behind a 4 KB header at `0x38000` (sector 56). The header carries a magic, the
  sizes and a build UUID; the same UUID is in the pack itself, and the game refuses to start (with a message) if the two
  halves do not belong together.

Sectors 0-55 of the external flash are never written by any of our code: 0-30 are the History, 31-55 are headroom for it.

## The loaders (`firmware/doomload`)

Two separate builds with the same USB stack (a polled F103-style full-speed device driver, bulk-only mass storage,
SCSI, a generated FAT16 volume):

* **backup (`build.sh`)**: the external flash as a 1 MB `NOR.BIN` plus an `INFO.TXT` with its CRC-32. Every write is refused. The
  sources contain no flash controller access and no write opcodes (checked by `grep` in the build).
* **write (`build_write.sh`)**: a blank FAT16 volume. A copied `doom.uf2` is recognised by content (UF2 blocks with
  family `0x34304753`, a 256-byte payload and the target as NOR offset), in any order, duplicates compared and not rewritten;
  a 4 KB sector is erased the first time a block for it arrives, every page is read back after programming. The only code
  that issues W25Q opcodes (`06`, `05`, `35`, `20`, `02`, `9F`) is `nor_write.c`; a single guard function limits every
  erase and program to `SG_NOR_MIN`..end of chip. Start-up refuses on a wrong JEDEC ID, on block-protect bits, and
  when the History CRC differs from the backup it was built with. Holding FN at power-on selects the erase mode.

Host tests (`firmware/doomload/host`) drive the real C sources against a model of the W25Q80: address guard, shuffled and
repeated blocks, interrupted sessions, failing chips, write protection and the complete `doom.uf2` through the
bulk-only transport.

## Engine changes

See `firmware/doom/dmc100/UPSTREAM.txt`. In short: a `--capacity` option of the converter, a return to the title
when the level ends (the intermission data is not in the pack) and a larger event queue (the stock ring of two events dropped key
releases when several keys changed in one call).
