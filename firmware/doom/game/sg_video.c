/*
 * Video side of the engine interface: the 3D view goes through the strip renderer and its pump (sg_lcd.c), the
 * status band through the compositor, both straight to the LCD; there is no screen buffer (I_VideoBuffer stays NULL).
 * Derived from port/game/i_dmc.c of DMC100-Doom (GPL-2.0-or-later, Artur Bieniek).
 */
#include "doomdef.h"
#include "i_video.h"

#include "fetch.h"
#include "lcd.h"
#include "r_strip.h"

byte *I_VideoBuffer;

void I_InitGraphics(void) {}
void I_StartFrame(void) {}
void I_UpdateNoBlit(void) {}

void I_SetPalette(int palette)
{
    R_PaletteChanged();
    pal_read(palette, pump_lut);
}

void I_FinishUpdate(void) {}
