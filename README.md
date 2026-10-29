# Doom on the FNIRSI SG-004A

**English** · [Русский](README.ru.md)

E1M1 of Doom on the FNIRSI SG-004A signal generator. Engine: [DMC100-Doom](https://github.com/ArturBieniek4/DMC100-Doom).
Freedoom art (the original art can be built from your own WAD, see [docs](docs/ORIGINAL_DOOM.md)). No sound, no saves.

> **At your own risk.** Tested on one unit, stock firmware V3.0_20230825.

## Keys

| Key | Level | Menu |
|---|---|---|
| UP / DOWN | forward / back | up / down |
| LEFT / OK | turn left / right | left / Enter |
| OUT / RIGHT | strafe left / right | – / right |
| IN | fire | Enter |
| FN | use | Esc |
| POWER | tap: menu, hold 2 s: off | |

## Install

Files from [Releases](../../releases) (or [built from source](docs/BUILD.md)). Firmware is copied onto the `BOOTLOADER`
USB drive, as with any `.bin` for this device.

1. **Back up** the external flash: copy `SG004_NORBACKUP.bin` to the `BOOTLOADER` drive, then copy `NOR.BIN` from the new
   drive somewhere safe. (Your measurement history is in it. The tool cannot write.)
2. **Data:** copy `SG004_NORLOAD.bin` to `BOOTLOADER`. The screen must show `JEDEC EF4014`. Copy `doom.uf2` to the new
   drive, wait for `DONE: verified` and `History unchanged`, eject, power off.
3. **Game:** copy `SG004_DOOM.bin` to `BOOTLOADER`.

**Back to stock:** copy FNIRSI's stock `.bin` to `BOOTLOADER`. Doom's data in the external flash stays there unused; to remove it,
power on `SG004_NORLOAD.bin` with FN held, then hold OK for 2 s.

What is written where, and why it is safe: [docs/INSTALL.md](docs/INSTALL.md).

## More

[Build](docs/BUILD.md) · [Original art](docs/ORIGINAL_DOOM.md) · [Design](docs/DESIGN.md) · GPL-2.0-or-later ([LICENSE](LICENSE), [NOTICE](NOTICE.md)); no game data in this repository.
