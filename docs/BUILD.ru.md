# Сборка из исходников

Нужны `sh`, `git`, Python 3, `clang` и GNU Arm Embedded (`arm-none-eabi-gcc`, `-ld`, `-objcopy`). На Windows подойдёт MSYS2
(`pacman -S mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-arm-none-eabi-gcc`); скрипты ищут инструменты в `/c/msys64/ucrt64/bin`.

```sh
scripts/build.sh freedoom
```

`NOR.BIN` это необязательная резервная копия внешней флеш-памяти. Чтобы её получить, запустите скрипт без `--backup`, возьмите
полученный `SG004_NORBACKUP.bin`, сделайте копию и запустите скрипт ещё раз уже с ней.

Скрипт скачивает Freedoom 0.13.0 (проверяет SHA-256) и нужные конвертеру исходники doomgeneric, конвертирует E1M1, собирает
игру и оба загрузчика и кладёт `SG004_NORBACKUP.bin`, `SG004_NORLOAD.bin`, `doom.uf2` и `SG004_DOOM.bin` в `out/`.

Тесты: в `firmware/doomload/host/` хост-тесты загрузчика (модель W25Q80, USB mass-storage и вся запись настоящего `doom.uf2`;
команды компиляции в заголовках файлов). `firmware/doom/game/tools/emu_run.py` запускает игру в эмуляторе Cortex-M4 (Unicorn).

Структура: `firmware/doom/game` (платформенный слой), `firmware/doom/dmc100` (движок, `UPSTREAM.txt` перечисляет наши правки),
`firmware/doom/tools` (инструменты для WAD), `firmware/doomload` (USB-загрузчики), `scripts/build.sh` (весь конвейер).
