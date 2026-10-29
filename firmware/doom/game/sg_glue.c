/*
 * Glue between the resident DMC100 engine and this platform: the parts of the DMC100 firmware that only matter with
 * overlays or with saves, the benchmark, the game pad and the USB drive are reduced to what the game needs here.
 * dmc_place_caches comes from port/game/i_dmc.c of DMC100-Doom (GPL-2.0-or-later, Artur Bieniek).
 */
#include <stddef.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "i_system.h"
#include "p_setup.h"

#include "cache.h"
#include "dmc_fw.h"
#include "fe.h"
#include "fetch.h"
#include "l_level.h"
#include "ovl.h"
#include "platform.h"
#include "r_strip.h"
#include "rec.h"

/* ---- automap: not in this build (am_map.c is left out) */
boolean automapactive;
void AM_Stop(void) {}
void AM_Ticker(void) {}
void AM_Drawer(void) {}
boolean AM_Responder(event_t *ev) { (void)ev; return false; }

/* ---- saves: need writes to the NOR, which this build does not do */
void G_DoSaveGame(void) {}
void G_DoLoadGame(void) {}
int G_SaveUsable(void) { return SAVE_ENOFILE; }

int G_SaveHeader(int slot, dmc_save_hdr_t *h)
{
    (void)slot; (void)h;
    return SAVE_ENOFILE;
}

int G_SaveCheck(int slot, dmc_save_hdr_t *h)
{
    (void)slot; (void)h;
    return SAVE_ENOFILE;
}

/* ---- the firmware's test settings: all zero */
const volatile dmc_test_t dmc_test;

/* ---- creation order for the benchmark's hash: not used */
void dmc_level_begin(void) {}
void dmc_level_end(void) {}

/* ---- libc pieces the engine links but does not run on the device */
int printf(const char *fmt, ...) { (void)fmt; return 0; }
int sscanf(const char *s, const char *fmt, ...) { (void)s; (void)fmt; return 0; }

/* Called by P_SetupLevel once the map header and its plan are loaded: the record cache and the flat slots follow the
   frame region of the plan, before the level arena is cleared. */
void dmc_place_caches(void)
{
    uint8_t *c = R_FrameSetup();
    uint32_t k = M.plan.k;
    uint8_t *arena = __ram_end - M.plan.arena_bytes;
    uint32_t need = RC_BYTES + k * DMC_FLAT_SIZE;
    if (c + need > arena)
        I_Error("plan %c: caches end %u B into the arena", 'A' + M.plan.plan, (unsigned)(c + need - arena));
    rc_place(c);
    flat_place(k ? c + RC_BYTES : NULL);
}
