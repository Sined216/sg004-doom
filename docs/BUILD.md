# Build from source

Needs `sh`, `git`, Python 3, `clang` and the GNU Arm Embedded tools (`arm-none-eabi-gcc`, `-ld`, `-objcopy`). On Windows use
MSYS2 (`pacman -S mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-arm-none-eabi-gcc`); the scripts look in `/c/msys64/ucrt64/bin`.

```sh
scripts/build.sh freedoom --backup NOR.BIN
```

`NOR.BIN` is your backup from the install step 1. Without a backup yet, run the script without `--backup`, use the
`SG004_NORBACKUP.bin` it makes to get one, then run it again with the backup.

The script downloads Freedoom 0.13.0 (SHA-256 checked) and the pinned doomgeneric source the converter needs, converts E1M1, builds the
game and both loaders, and puts `SG004_NORBACKUP.bin`, `SG004_NORLOAD.bin`, `doom.uf2` and `SG004_DOOM.bin` into `out/`.

Tests: `firmware/doomload/host/` has host tests of the loader (a model of the W25Q80, the USB mass-storage transport and the
whole write flow with a real `doom.uf2`; compile commands are in the file headers). `firmware/doom/game/tools/emu_run.py` runs the game
in a Cortex-M4 emulator (Unicorn).

Layout: `firmware/doom/game` (platform layer), `firmware/doom/dmc100` (vendored engine, `UPSTREAM.txt` lists our changes),
`firmware/doom/tools` (WAD tools), `firmware/doomload` (USB loaders), `scripts/build.sh` (pipeline).
