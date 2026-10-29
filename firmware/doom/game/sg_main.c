/*
 * SG-004A Doom: boot shell. Brings up power, clock, LCD and keys, checks the game data on the external NOR (read
 * only), then hands over to the engine (D_DoomMain, never returns). Nothing in this firmware writes to flash, the
 * NOR, the EEPROM or the option bytes, and no analog-stage pin is touched.
 */
#include <stdint.h>

#include "board.h"
#include "clock.h"
#include "lcd.h"
#include "nor.h"
#include "platform.h"

#include "d_main.h"
#include "cache.h"
#include "dmc_fw.h"
#include "fetch.h"
#include "img.h"
#include "m_argv.h"

#include "sg_layout.h"

extern int sg_data_status;

extern int lcd_ready;
void sg_input_init(void);

static void gpio_mode(uint32_t port, uint32_t pin, uint32_t mode)
{
    volatile uint32_t *cr = (volatile uint32_t *)(port + (pin < 8 ? GPIO_CRL : GPIO_CRH));
    uint32_t sh = (pin & 7) * 4;
    *cr = (*cr & ~(0xFu << sh)) | (mode << sh);
}

static void input_pullup(uint32_t port, uint32_t pin)
{
    REG(port + GPIO_BSRR) = 1u << pin;
    gpio_mode(port, pin, GM_IN_PULL);
}

static void say(int row, const char *s, uint16_t fg)
{
    lcd_text(0, row * 18, s, fg, 0x0000, 2);
}

static void hex(char *p, uint32_t v)
{
    static const char d[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--, v >>= 4) p[i] = d[v & 0xF];
    p[8] = 0;
}

/* a message that stays: the SysTick power watchdog still switches the device off with a 2 s hold of POWER */
static __attribute__((noreturn)) void stop(void)
{
    for (;;) IWDG_KR = 0xAAAAu;
}

/* the engine's command line, written by build_game.sh into sg_args.h (default "doom": title screen and menu) */
#include "sg_args.h"

int main(void)
{
    /* Hold power first, before anything that could take time */
    RCC_APB2ENR |= (1u << 0) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6);
    REG(GPIOB_BASE + GPIO_BSRR) = 1u << PWR_HOLD_PIN;
    gpio_mode(PWR_HOLD_PORT, PWR_HOLD_PIN, GM_OUT_PP_2M);
    AFIO_MAPR = (AFIO_MAPR & ~(7u << 24)) | (2u << 24);   /* JTAG off (frees PB3/PB4), SWD stays on */

    input_pullup(PWR_KEY_PORT, PWR_KEY_PIN);
    input_pullup(KEY_IN_PORT, KEY_IN_PIN);       input_pullup(KEY_OUT_PORT, KEY_OUT_PIN);
    input_pullup(KEY_FN_PORT, KEY_FN_PIN);       input_pullup(KEY_UP_PORT, KEY_UP_PIN);
    input_pullup(KEY_DOWN_PORT, KEY_DOWN_PIN);   input_pullup(KEY_LEFT_PORT, KEY_LEFT_PIN);
    input_pullup(KEY_RIGHT_PORT, KEY_RIGHT_PIN); input_pullup(KEY_OK_PORT, KEY_OK_PIN);

    /* the bootloader may have left peripheral interrupts enabled; this firmware uses only SysTick */
    for (int i = 0; i < 3; i++) {
        REG(0xE000E180u + 4u * i) = 0xFFFFFFFFu;    /* NVIC_ICER */
        REG(0xE000E280u + 4u * i) = 0xFFFFFFFFu;    /* NVIC_ICPR */
    }

    REG(BL_PORT + GPIO_BSRR) = 1u << BL_PIN;        /* dark until the panel is initialised */
    gpio_mode(BL_PORT, BL_PIN, GM_OUT_PP_2M);

    /* FN held at power-on keeps the core on HSI 8 MHz (safe mode for supply problems at 240 MHz) */
    for (volatile int i = 0; i < 1000; i++) {}
    clock_init((REG(KEY_FN_PORT + GPIO_IDR) >> KEY_FN_PIN) & 1u);
    lcd_init(0);
    lcd_fill(0, 0, LCD_W - 1, LCD_H - 1, 0x0000);
    lcd_backlight(1);

    say(0, "SG-004A DOOM", 0xFFE0);
    sg_input_init();

    if (nor_init() != 0) {
        say(2, "NO SPI FLASH FOUND", 0xF800);
        stop();
    }
    char b[24] = "JEDEC 00";
    hex(b + 6, nor_jedec());
    say(1, b, 0xFFFF);

    /* the record cache before the first level: behind the smallest frame region, where the level setup does not
       reach (the DMC100 firmware does the same in its BOOT overlay) */
    rc_place(DMC_FRAME_BASE + PLAT_FRAME_MIN);
    flat_place(NULL);
    flat_set_slots(0);

    int rc = img_init();
    if (rc != IMG_OK) {
        char m[24] = "NO GAME DATA, CODE ";
        m[19] = (char)('0' + (-rc) % 10);
        m[20] = 0;
        say(3, m, 0xF800);
        if (sg_data_status == 2) {
            say(5, "NOR data and game", 0xFFFF);
            say(6, "program do not match:", 0xFFFF);
            say(7, "write both again", 0xFFFF);
        } else {
            say(5, "Data must be on the", 0xFFFF);
            say(6, "SPI flash at 0x", 0xFFFF);
            char a[12];
            hex(a, SG_NOR_MIN);
            say(7, a, 0xFFFF);
        }
        stop();
    }
    say(3, "Starting...", 0x07E0);

    static char *av[] = { SG_ARGS, 0 };
    myargc = (int)(sizeof av / sizeof av[0]) - 1;
    myargv = av;
    D_DoomMain();
    stop();
}
