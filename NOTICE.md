# Лицензии и авторские права

## Данные игры

В этом репозитории нет данных Doom: только исходный код и скрипты сборки. Файлы `DOOM.WAD` и всё, что скрипты из него
получают (`doom.uf2`, `SG004_DOOM.bin`, `out/`, `gen/`, `packs/`), нельзя коммитить и распространять, если они собраны из WAD Doom.

* **Сборка на Freedoom.** Данные берутся из [Freedoom](https://freedoom.github.io/) Phase 1 0.13.0 (лицензия BSD-3-Clause,
  текст в архиве Freedoom: `COPYING.txt`, авторы: `CREDITS.txt`). Файлы, собранные из него, можно распространять, если вместе с
  ними идут уведомление об авторских правах, текст лицензии и отказ от гарантий. Релиз с такими файлами должен содержать
  `COPYING.txt` и `CREDITS.txt` Freedoom. Название проекта Freedoom нельзя использовать для продвижения результата.
* **Сборка на оригинальном Doom.** Нужен шареварный `DOOM1.WAD` 1.9 (его бесплатно распространяют в виде шареварного архива
  id Software) или ваша копия купленной игры. Скрипты скачивают и проверяют шареварный архив и конвертируют его у вас на
  компьютере. Полученные данные и образы содержат защищённые авторским правом графику, музыку и карты: используйте их на своём
  приборе и не публикуйте.

## Программы

* Движок в `firmware/doom/dmc100`: [DMC100-Doom](https://github.com/ArturBieniek4/DMC100-Doom) Артура Бенека (Artur Bieniek),
  лицензия GPL-2.0-or-later (`firmware/doom/dmc100/COPYING`, `LICENSES/`, `CREDITS.md`). Он основан на
  [doomgeneric](https://github.com/ozkl/doomgeneric) и исходном коде Doom от id Software (GPL-2.0-or-later). Наши изменения
  перечислены в `firmware/doom/dmc100/UPSTREAM.txt`.
* Всё остальное (платформенный слой, загрузчики, скрипты, документация) тоже под GPL-2.0-or-later, см. `LICENSE`.
* Конвертер во время сборки скачивает нужные ему исходники doomgeneric (`dmc100/oracle/fetch.sh`); в репозиторий они не входят.

## Товарные знаки, отсутствие связи и гарантий

FNIRSI является товарным знаком своего владельца, Doom: товарным знаком id Software / ZeniMax. Проект не связан с ними и не одобрен
ими. Программа поставляется «как есть», без каких-либо гарантий (разделы 11 и 12 GPL). Прошивка устройства, которым вы владеете,
это ваше решение и ваш риск.

---

# Notice

## Game data

This repository contains no game data: source code and build scripts only. It contains no `DOOM.WAD`, and none of the files the
scripts produce from one (`doom.uf2`, `SG004_DOOM.bin`, `out/`, `gen/`, `packs/`) may be committed or redistributed when built from a
Doom WAD.

* **Freedoom build.** The data comes from [Freedoom](https://freedoom.github.io/) Phase 1 0.13.0, which is BSD-3-Clause (the text
  is in the Freedoom archive as `COPYING.txt`, contributors in `CREDITS.txt`). Files built from it may be redistributed provided that
  the copyright notice, the licence text and the disclaimer travel with them. A release containing such files must include the
  Freedoom `COPYING.txt` and `CREDITS.txt`. The name of the Freedoom project must not be used to promote the result.
* **Original Doom build.** Needs the shareware `DOOM1.WAD` 1.9 (free to download and distribute *as the shareware archive*, from
  the id Software releases), or your own copy of the registered game. The scripts download and check the shareware archive and
  convert it on your machine. The converted data and the images made from it are derived from copyrighted art, music and maps: use
  them on your own device and do not publish them.

## Software

* The engine in `firmware/doom/dmc100` is [DMC100-Doom](https://github.com/ArturBieniek4/DMC100-Doom) by Artur Bieniek
  (GPL-2.0-or-later; see `firmware/doom/dmc100/COPYING`, `LICENSES/`, `CREDITS.md`), which in turn is
  [doomgeneric](https://github.com/ozkl/doomgeneric) and id Software's Doom source release (GPL-2.0-or-later).
  Our changes to it are listed in `firmware/doom/dmc100/UPSTREAM.txt`.
* Everything else (platform layer, loaders, scripts, documentation) is licensed under GPL-2.0-or-later as well, see `LICENSE`.
* The converter fetches the pinned upstream doomgeneric sources at build time (`dmc100/oracle/fetch.sh`); they are not part of
  this repository.

## Trademarks, no affiliation, no warranty

FNIRSI is a trademark of its owner; Doom is a trademark of id Software / ZeniMax. This project is not affiliated with or endorsed by
either. The software is provided as is, without warranty of any kind (GPL sections 11 and 12). Flashing firmware to a device you
own is your decision and your risk.
