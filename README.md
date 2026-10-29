# Doom на FNIRSI SG-004A

Первый уровень Doom (E1M1) на генераторе сигналов FNIRSI SG-004A. Движок: [DMC100-Doom](https://github.com/ArturBieniek4/DMC100-Doom).
Графика Freedoom (оригинальную можно собрать из вашего WAD, см. [docs](docs/ORIGINAL_DOOM.ru.md)). Звука и сохранений нет.

> **На свой риск.** Проверено на одном приборе, стоковая прошивка V3.0_20230825.

**Клавиши**

| Клавиша | Уровень | Меню |
|---|---|---|
| UP / DOWN | вперёд / назад | вверх / вниз |
| LEFT / OK | поворот влево / вправо | влево / Enter |
| OUT / RIGHT | шаг влево / вправо | – / вправо |
| IN | огонь | Enter |
| FN | использовать | Esc |
| POWER | коротко: меню, 2 с: выключить | |

**Установка.** Файлы из [Releases](../../releases) (или [собранные из исходников](docs/BUILD.ru.md)) копируются на USB-диск
`BOOTLOADER`, как любой `.bin` для этого прибора.

1. **Данные:** скопируйте `SG004_NORLOAD.bin` на `BOOTLOADER`. На экране должно быть `JEDEC EF4014`. Скопируйте `doom.uf2` на
   новый диск, дождитесь `DONE: verified` и `History unchanged`, извлеките диск, выключите прибор.
2. **Игра:** скопируйте `SG004_DOOM.bin` на `BOOTLOADER`.

**Вернуть стоковую прошивку:** скопируйте стоковый `.bin` FNIRSI на `BOOTLOADER`. Данные Doom во внешней флеш-памяти останутся
неиспользованными; чтобы стереть их, включите `SG004_NORLOAD.bin` с зажатой FN и удерживайте OK 2 с.
Что и куда пишется: [docs/INSTALL.ru.md](docs/INSTALL.ru.md). Сборка: [docs/BUILD.ru.md](docs/BUILD.ru.md).
GPL-2.0-or-later ([LICENSE](LICENSE), [NOTICE](NOTICE.md)); данных игры в репозитории нет.

---

# Doom on the FNIRSI SG-004A

E1M1 of Doom on the FNIRSI SG-004A signal generator. Engine: [DMC100-Doom](https://github.com/ArturBieniek4/DMC100-Doom).
Freedoom art (the original art can be built from your own WAD, see [docs](docs/ORIGINAL_DOOM.md)). No sound, no saves.

> **At your own risk.** Tested on one unit, stock firmware V3.0_20230825.

**Keys**

| Key | Level | Menu |
|---|---|---|
| UP / DOWN | forward / back | up / down |
| LEFT / OK | turn left / right | left / Enter |
| OUT / RIGHT | strafe left / right | – / right |
| IN | fire | Enter |
| FN | use | Esc |
| POWER | tap: menu, hold 2 s: off | |

**Install.** Files from [Releases](../../releases) (or [built from source](docs/BUILD.md)) are copied onto the `BOOTLOADER`
USB drive, as with any `.bin` for this device.

1. **Data:** copy `SG004_NORLOAD.bin` to `BOOTLOADER`. The screen must show `JEDEC EF4014`. Copy `doom.uf2` to the new
   drive, wait for `DONE: verified` and `History unchanged`, eject, power off.
2. **Game:** copy `SG004_DOOM.bin` to `BOOTLOADER`.

**Back to stock:** copy FNIRSI's stock `.bin` to `BOOTLOADER`. Doom's data in the external flash stays there unused; to remove it,
power on `SG004_NORLOAD.bin` with FN held, then hold OK for 2 s.
What is written where: [docs/INSTALL.md](docs/INSTALL.md). Build: [docs/BUILD.md](docs/BUILD.md).
GPL-2.0-or-later ([LICENSE](LICENSE), [NOTICE](NOTICE.md)); no game data in this repository.
