# Notice

*По-русски, кратко:* в репозитории нет данных игры, только исходники и скрипты. Сборку из Freedoom (BSD-3-Clause) можно
распространять, приложив `COPYING.txt` и `CREDITS.txt` Freedoom. Данные, собранные из `DOOM1.WAD`, защищены авторским правом:
используйте их на своём приборе и не публикуйте. Движок под GPL-2.0-or-later. Гарантий нет, прошивка на ваш риск.

## This repository contains no game data

Doom is a copyrighted game. This repository holds source code and build scripts only. It contains no `DOOM.WAD`,
and none of the files the scripts produce from one (`doom.uf2`, `SG004_DOOM.bin`, `out/`, the converter's `gen/`
and `packs/`) is to be committed or redistributed when built from a Doom WAD.

* **Freedoom build.** The data comes from [Freedoom](https://freedoom.github.io/) Phase 1 0.13.0, which is
  BSD-3-Clause (the text is in the Freedoom archive as `COPYING.txt`; contributors in `CREDITS.txt`). Files built
  from it may be redistributed provided that the copyright notice, the licence text and the disclaimer travel with them.
  A release containing such files must include the Freedoom `COPYING.txt` and `CREDITS.txt` next to them. The name
  of the Freedoom project must not be used to promote the result.
* **Original Doom build.** Needs the shareware `DOOM1.WAD` 1.9 (free to download and distribute *as the shareware
  archive*, from the id Software releases), or your own copy of the registered game. The scripts download and
  check the shareware archive and convert it on your machine. The converted data and the images made from it are derived
  from copyrighted art, music and maps: use them on your own device and do not publish them.

## Software

* The engine in `firmware/doom/dmc100` is [DMC100-Doom](https://github.com/ArturBieniek4/DMC100-Doom) by Artur Bieniek
  (GPL-2.0-or-later; see `firmware/doom/dmc100/COPYING`, `LICENSES/`, `CREDITS.md`), which in turn is
  [doomgeneric](https://github.com/ozkl/doomgeneric) and id Software's Doom source release (GPL-2.0-or-later).
  Our changes to it are listed in `firmware/doom/dmc100/UPSTREAM.txt`.
* Everything else (platform layer, loaders, scripts, documentation) is licensed under GPL-2.0-or-later as well,
  see `LICENSE`.
* The converter fetches the pinned upstream doomgeneric sources at build time (`dmc100/oracle/fetch.sh`); they are not
  part of this repository.

## Trademarks, no affiliation, no warranty

FNIRSI is a trademark of its owner; Doom is a trademark of id Software / ZeniMax. This project is not affiliated
with or endorsed by either. The software is provided as is, without warranty of any kind (GPL sections 11 and 12).
Flashing firmware to a device you own is your decision and your risk.
