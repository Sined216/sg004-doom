/*
 * Engine system interface of the SG-004A build: time, errors, zone, sound stubs, the platform functions the DMC100
 * code calls (plat.h) and the few globals its firmware glue would provide. Derived from port/game/i_dmc.c and
 * host/main_host.c of DMC100-Doom (GPL-2.0-or-later, Artur Bieniek); the overlay machinery is not used here, the
 * code is resident (native overlay tables, PLAT_HOST).
 */
#include <stdarg.h>
#include <stddef.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "d_main.h"
#include "i_sound.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "sounds.h"
#include "z_zone.h"

#include "board.h"
#include "clock.h"
#include "dmc_fw.h"
#include "fe.h"
#include "platform.h"

/* ---------------------------------------------------------------- platform */

uint32_t plat_ms(void) { return ms_ticks; }
void plat_delay_ms(uint32_t ms) { delay_ms(ms); }
void plat_idle(void) {}
uint32_t plat_cycles(void) { return 0; }

__attribute__((noreturn)) void plat_power_off(void)
{
    REG(GPIOC_BASE + GPIO_BSRR) = 1u << 6;              /* backlight off */
    REG(GPIOB_BASE + GPIO_BRR) = 1u << 7;               /* drop the power latch; power goes once the key is released */
    for (;;) IWDG_KR = 0xAAAAu;
}

uint32_t plat_stack_used(void) { return 0; }

/* The DMC100 buttons do not exist here: the keys are read by sg_input.c */
uint32_t plat_buttons(void) { return 0; }
int plat_event(void) { return -1; }

/* snprintf subset: %s %c %d %i %u %x %X with '-', '0' and a width */
int plat_vfmt(char *out, int cap, const char *f, va_list ap)
{
    int n = 0;
#define PUT(ch) do { if (n + 1 < cap) out[n] = (char)(ch); n++; } while (0)
    for (; *f; f++) {
        if (*f != '%') { PUT(*f); continue; }
        f++;
        int left = 0, zero = 0, width = 0;
        for (;; f++) {
            if (*f == '-') left = 1;
            else if (*f == '0') zero = 1;
            else break;
        }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        char tmp[12];
        const char *s = tmp;
        int len = 0;
        switch (*f) {
        case 's': s = va_arg(ap, const char *); if (!s) s = "(null)"; while (s[len]) len++; break;
        case 'c': tmp[0] = (char)va_arg(ap, int); len = 1; break;
        case 'd': case 'i': case 'u': {
            unsigned v;
            int neg = 0;
            if (*f == 'u') v = va_arg(ap, unsigned);
            else { int x = va_arg(ap, int); neg = x < 0; v = neg ? 0u - (unsigned)x : (unsigned)x; }
            char r[12]; int k = 0;
            do { r[k++] = (char)('0' + v % 10); v /= 10; } while (v);
            if (neg) r[k++] = '-';
            while (k) tmp[len++] = r[--k];
            break;
        }
        case 'x': case 'X': {
            unsigned v = va_arg(ap, unsigned);
            char r[12]; int k = 0;
            do { unsigned d = v & 15u; r[k++] = (char)(d < 10 ? '0' + d : (*f == 'x' ? 'a' : 'A') + d - 10); v >>= 4; } while (v);
            while (k) tmp[len++] = r[--k];
            break;
        }
        case '%': tmp[0] = '%'; len = 1; break;
        default: PUT('?'); continue;
        }
        if (!left) for (int i = len; i < width; i++) PUT(zero ? '0' : ' ');
        for (int i = 0; i < len; i++) PUT(s[i]);
        if (left) for (int i = len; i < width; i++) PUT(' ');
    }
#undef PUT
    if (cap > 0) out[n < cap ? n : cap - 1] = 0;
    return n;
}

int plat_fmt(char *out, int cap, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    int n = plat_vfmt(out, cap, f, ap);
    va_end(ap);
    return n;
}

/* ---------------------------------------------------------------- errors */

void I_Error(char *error, ...)
{
    char msg[64];
    va_list ap;
    va_start(ap, error);
    plat_vfmt(msg, sizeof msg, error, ap);
    va_end(ap);
    plat_panic(msg);
    for (;;) {}
}

/* The return address names the check that failed */
void dmc_fatal(const char *msg)
{
    I_Error("%s at %x", (char *)msg, (unsigned)(uintptr_t)__builtin_return_address(0));
}

void I_Quit(void) { plat_power_off(); }

void I_Tactile(int on, int off, int total) { (void)on; (void)off; (void)total; }

/* ---------------------------------------------------------------- time */

int I_GetTimeMS(void) { return (int)ms_ticks; }

int I_GetTime(void)
{
    uint32_t ms = ms_ticks;
    return (int)(ms / 1000u * TICRATE + ms % 1000u * TICRATE / 1000u);
}

void I_Sleep(int ms) { delay_ms(ms > 0 ? (uint32_t)ms : 1u); }
void I_InitTimer(void) {}
void I_WaitVBL(int count) { delay_ms((uint32_t)count * 1000u / 70u); }

/* ---------------------------------------------------------------- zone: the only user is S_Init's channel table */

#define POOL_BYTES 256u
static uint8_t pool[POOL_BYTES] __attribute__((aligned(4)));
static uint32_t pool_used;

void Z_Init(void) {}
void Z_FreeTags(int lowtag, int hightag) { (void)lowtag; (void)hightag; }
void Z_CheckHeap(void) {}

void *Z_Malloc(int size, int tag, void *user)
{
    uint32_t n = ((uint32_t)size + 3u) & ~3u;
    if (tag != PU_STATIC || user || n > POOL_BYTES - pool_used)
        I_Error("Z_Malloc %d B, %u B left", size, (unsigned)(POOL_BYTES - pool_used));
    void *p = pool + pool_used;
    pool_used += n;
    return p;
}

/* ---------------------------------------------------------------- sound: none yet */

void I_InitSound(boolean use_sfx_prefix) { (void)use_sfx_prefix; }
void I_UpdateSound(void) {}

int I_GetSfxLumpNum(sfxinfo_t *sfxinfo)
{
    if (sfxinfo->link)
        sfxinfo = &S_sfx[sfxinfo->link];
    return (int)(sfxinfo - S_sfx);
}

int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    (void)sfxinfo; (void)vol; (void)sep;
    return channel;
}

void I_StopSound(int handle) { (void)handle; }
boolean I_SoundIsPlaying(int handle) { (void)handle; return false; }
void I_UpdateSoundParams(int handle, int vol, int sep) { (void)handle; (void)vol; (void)sep; }
void snd_volume(int vol) { (void)vol; }

/* ---------------------------------------------------------------- settings and demos: none */

dmc_set_t dmc_set = { .sound = SND_OFF };
int dmc_render = 1;

int settings_save(const void *src, uint32_t size, uint16_t version)
{
    (void)src; (void)size; (void)version;
    return 0;
}

int dmc_demo_open(const char *name, uint32_t *size) { (void)name; (void)size; return -1; }

void dmc_demo_read(uint32_t off, void *dst, uint32_t n)
{
    (void)off; (void)dst; (void)n;
    I_Error("no demo file");
}
