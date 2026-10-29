/*
 * Keys of the SG-004A as a keyboard for the engine. The nine keys are sampled and debounced at 1 kHz (SysTick), edges
 * are latched and posted as vanilla key events from I_StartTic, one resolution per press (the key it sent goes up
 * when the physical key is released), so a context change while a key is held cannot leave a key stuck.
 *
 *   key        in a level          menu / prompt / title / screens
 *   UP, DOWN   forward, backward   up, down
 *   LEFT       turn left           left
 *   RIGHT      strafe right        right
 *   OK         turn right          Enter (y in a prompt)
 *   FN         use / open          Escape (n in a prompt)
 *   IN         fire                Enter (y in a prompt)
 *   OUT        strafe left         -
 *   POWER tap  Escape: open / close the menu or go back (POWER held 2 s switches the device off, in SysTick)
 *
 * Always-run is on (key_speed = 256).
 */
#include <stdint.h>

#include "board.h"
#include "clock.h"

#include "d_event.h"
#include "doomdef.h"
#include "doomkeys.h"
#include "doomstat.h"
#include "m_controls.h"
#include "m_menu.h"

#include "pad.h"

boolean D_WillBuildTic(void);

enum { K_IN, K_OUT, K_FN, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_OK, K_COUNT };

static const struct { uint32_t port; uint8_t pin; } pins[K_COUNT] = {
    [K_IN] = {KEY_IN_PORT, KEY_IN_PIN},       [K_OUT] = {KEY_OUT_PORT, KEY_OUT_PIN},
    [K_FN] = {KEY_FN_PORT, KEY_FN_PIN},       [K_UP] = {KEY_UP_PORT, KEY_UP_PIN},
    [K_DOWN] = {KEY_DOWN_PORT, KEY_DOWN_PIN}, [K_LEFT] = {KEY_LEFT_PORT, KEY_LEFT_PIN},
    [K_RIGHT] = {KEY_RIGHT_PORT, KEY_RIGHT_PIN}, [K_OK] = {KEY_OK_PORT, KEY_OK_PIN},
};

#define DEBOUNCE 4              /* samples, ms */
#define POWER_TAP_MAX_MS 500u

static uint8_t cnt[K_COUNT];                    /* integrator 0..DEBOUNCE */
static uint8_t state;                           /* debounced, bit k = key k down */
static volatile uint8_t press_latch, release_latch;

static uint8_t pwr_cnt, pwr_state;
static uint32_t pwr_down_ms;
static volatile uint8_t pwr_tap;

/* called every millisecond from SysTick_Handler */
void sg_input_sample(void)
{
    for (int k = 0; k < K_COUNT; k++) {
        int raw = !((REG(pins[k].port + GPIO_IDR) >> pins[k].pin) & 1u);    /* active low */
        if (raw) { if (cnt[k] < DEBOUNCE) cnt[k]++; }
        else     { if (cnt[k] > 0) cnt[k]--; }
        uint8_t bit = (uint8_t)(1u << k);
        if (cnt[k] == DEBOUNCE && !(state & bit)) { state |= bit; press_latch |= bit; }
        else if (cnt[k] == 0 && (state & bit))    { state &= (uint8_t)~bit; release_latch |= bit; }
    }
    /* POWER: a short press and release is a tap (menu); the long hold is handled in SysTick */
    int raw = !((REG(GPIOB_BASE + GPIO_IDR) >> 8) & 1u);
    if (raw) { if (pwr_cnt < DEBOUNCE) pwr_cnt++; } else if (pwr_cnt) pwr_cnt--;
    if (pwr_cnt == DEBOUNCE && !pwr_state) { pwr_state = 1; pwr_down_ms = ms_ticks; }
    else if (pwr_cnt == 0 && pwr_state) {
        pwr_state = 0;
        if (ms_ticks - pwr_down_ms < POWER_TAP_MAX_MS && pwr_down_ms > 600u) pwr_tap = 1;   /* not the press that started the device */
    }
}

static uint8_t own[K_COUNT];                    /* key each physical key sent, 0 none */
static uint8_t esc_up;                          /* the Escape of a POWER tap goes up one call later */

static void post(evtype_t type, int key)
{
    event_t ev = {.type = type, .data1 = key};
    D_PostEvent(&ev);
}

static int resolve(int k)
{
    int menu = menuactive || messageToPrint;
    int level = gamestate == GS_LEVEL && !demoplayback && !menu;
    switch (k) {
    case K_UP:    return KEY_UPARROW;
    case K_DOWN:  return KEY_DOWNARROW;
    case K_LEFT:  return KEY_LEFTARROW;
    case K_RIGHT: return level ? KEY_STRAFE_R : KEY_RIGHTARROW;
    case K_OK:    return level ? KEY_RIGHTARROW : (messageToPrint && messageNeedsInput ? 'y' : KEY_ENTER);
    case K_FN:    return level ? KEY_USE : (messageToPrint && messageNeedsInput ? 'n' : KEY_ESCAPE);
    case K_IN:    return level ? KEY_FIRE : (messageToPrint && messageNeedsInput ? 'y' : KEY_ENTER);
    case K_OUT:   return level ? KEY_STRAFE_L : 0;
    }
    return 0;
}

void I_InputTic(void)
{
    if (!D_WillBuildTic()) return;                  /* the edges wait in the latches */

    if (esc_up) { esc_up = 0; post(ev_keyup, KEY_ESCAPE); }

    __asm volatile("cpsid i");
    uint8_t pr = press_latch, rl = release_latch, st = state;
    press_latch = 0;
    release_latch = 0;
    uint8_t tap = pwr_tap;
    pwr_tap = 0;
    __asm volatile("cpsie i");

    /* The engine's view (own[k]) is made to follow the debounced state, so a lost or reordered edge cannot leave a key
       stuck down. The latches only add what the state cannot show: a press and release between two calls, and a
       release followed by a press. */
    for (int k = 0; k < K_COUNT; k++) {
        uint8_t bit = (uint8_t)(1u << k);
        int down = (st & bit) != 0;
        if (own[k] && (!down || (rl & bit))) { post(ev_keyup, own[k]); own[k] = 0; }
        if (!own[k] && (down || (pr & bit))) {
            int key = resolve(k);
            if (key) { own[k] = (uint8_t)key; post(ev_keydown, key); }      /* a short tap goes up in the next call */
        }
    }
    if (tap) { post(ev_keydown, KEY_ESCAPE); esc_up = 1; }
}

void I_StartTic(void) { I_InputTic(); }

void sg_input_init(void)
{
    key_speed = 256;                                /* always run: g_game.c treats key_speed >= NUMKEYS as held */
}

/* No game pad here; g_game.c reads these */
volatile pad_t dmc_pad;

int II_PadAxis(int v, int max)
{
    (void)max;
    return v;
}
