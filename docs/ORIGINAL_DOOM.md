# Running the original Doom art

The repository does not contain Doom's data and the prebuilt releases do not either. To get the original look, build the
data on your own machine from the shareware episode (or from your own copy of `DOOM1.WAD`). The result is for your own
device: do not publish it (see [NOTICE.md](../NOTICE.md)).

1. Do the backup step of the main [README](../README.md#install) first (`SG004_NORBACKUP.bin`, keep `NOR.BIN`).
2. Build, with your backup, either from the downloaded shareware WAD (the archive and the WAD are checked against known SHA-256/SHA-1)

   ```sh
   scripts/build.sh original --backup NOR.BIN
   ```

   or from a shareware `DOOM1.WAD` 1.9 you already have (the registered game's `DOOM.WAD` has not been tried)

   ```sh
   scripts/build.sh original --wad /path/to/DOOM1.WAD --backup NOR.BIN
   ```

   The script cuts out the screens a one-map build never shows (intermission, help, credits, finale pictures), converts
   E1M1 into a data pack of about 1.06 MB and builds `out/doom.uf2` and `out/SG004_DOOM.bin`. The converter's size ladder
   may lower the sprite resolution of the monsters to make it fit.
3. Install exactly like the Freedoom build: `out/SG004_NORLOAD.bin`, then `out/doom.uf2` onto its drive, then
   `out/SG004_DOOM.bin`.

If you already ran the Freedoom build, just load the new `doom.uf2` and `SG004_DOOM.bin` over it. The loader erases the
sectors it needs and the game checks that both halves of the data come from the same build, and says so on screen if
they do not (for example after copying only `SG004_DOOM.bin`).

The pack does not fit if you change to another map or add textures: the data area is 1,073,152 bytes
(`firmware/doom/game/sg_layout.h`, `--capacity` in `scripts/build.sh`). The converter reports the size and stops if
the pack does not fit.
