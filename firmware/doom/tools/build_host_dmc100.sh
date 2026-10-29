#!/bin/sh
# Own build of the DMC100-Doom host engine (no upstream Makefile): compiles the sources listed in mk/engine.mk
# with clang into $OUT and links dmcdoom. Source tree is read from $SRC, generated tables from $GEN.
set -e
export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH"
SRC=${SRC:?}
GEN=${GEN:?}
OUT=${OUT:?}
mkdir -p "$OUT/obj"
cd "$SRC"

CF="-fwrapv -fsigned-char -std=gnu11 -g -O2 -w -Iengine -Iport/data -Iport/render -Iport/obj -Iport/math -Iplatform -Iport/game -Iport/ovl -I$GEN -Ihost -Ioracle -Itests/lockstep -DDMC_STATS -DDMC_RSTATS -DDMC_HOST -DR_FRAME_HOST=196608"

SRCS=""
for f in engine/*.c; do
  case $f in engine/w_wad.c|engine/w_file.c) continue;; esac
  SRCS="$SRCS $f"
done
SRCS="$SRCS $(ls port/data/*.c port/render/*.c port/obj/*.c) port/math/trig.c"
SRCS="$SRCS host/main_host.c host/nor_file.c host/stats.c host/i_video_host.c oracle/png.c tests/lockstep/hash.c tests/lockstep/play.c tests/lockstep/keys.c port/game/i_input.c"
SRCS="$SRCS port/game/fe_glue.c port/game/l_menu.c port/game/l_wi.c port/game/l_finale.c port/game/l_save.c port/game/g_save.c port/ovl/ovl.c port/ovl/ovl_file.c"

OBJS=""
for f in $SRCS; do
  o="$OUT/obj/$(echo "$f" | tr '/' '_').o"
  OBJS="$OBJS $o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
    X=""
    case $f in port/game/*|port/ovl/*) X="-DPLAT_HOST";; esac
    clang $CF $X -c "$f" -o "$o" || { echo "FAILED: $f" >&2; exit 1; }
  fi
done
for g in info colormap trig_tables view; do
  o="$OUT/obj/gen_$g.o"
  OBJS="$OBJS $o"
  clang -fwrapv -fsigned-char -std=gnu11 -g -O2 -w -I"$GEN" -DDMC_HOST -c "$GEN/$g.c" -o "$o"
done

WRAP="-Wl,--wrap=G_Ticker,--wrap=G_BuildTiccmd,--wrap=R_RenderPlayerView -Wl,--wrap=P_PlayerThink,--wrap=G_ExitLevel,--wrap=G_SecretExitLevel,--wrap=I_StartTic -Wl,--wrap=TryRunTics,--wrap=nor_erase,--wrap=nor_program"
clang $WRAP -Wl,--allow-multiple-definition -o "$OUT/dmcdoom.exe" $OBJS -lm
ls -la "$OUT/dmcdoom.exe"
