/*
 * Fatal screen: red background with the exception number or the engine's panic message, stacked PC/LR and fault
 * status registers (locate them in build/doom.map). The power key keeps working: a 2 s hold drops the latch.
 */
#include <stdint.h>
#include "board.h"
#include "lcd.h"
#include "platform.h"

#define SCB_MMFAR REG(0xE000ED34u)
#define SCB_BFAR  REG(0xE000ED38u)
#define SCB_CFSR_ REG(0xE000ED28u)
#define SCB_HFSR_ REG(0xE000ED2Cu)

extern int lcd_ready;

static char panic_msg[64];

static void hex(char *p, uint32_t v)
{
    static const char d[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--, v >>= 4) p[i] = d[v & 0xF];
    p[8] = 0;
}

static void line(int row, const char *label, uint32_t v)
{
    char b[24];
    int n = 0;
    while (label[n] && n < 8) { b[n] = label[n]; n++; }
    hex(b + n, v);
    lcd_text(0, row * 18, b, 0xFFFF, 0xF800, 2);
}

static void text_line(int row, const char *s)
{
    char b[21];
    int n = 0;
    while (s[n] && n < 20) { b[n] = s[n]; n++; }
    while (n < 20) b[n++] = ' ';
    b[n] = 0;
    lcd_text(0, row * 18, b, 0xFFFF, 0xF800, 2);
}

void fault_screen(const uint32_t *frame, uint32_t exc_return)
{
    uint32_t ipsr;
    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    REG(GPIOC_BASE + GPIO_BRR) = 1u << 6;             /* backlight on */
    if (lcd_ready) {
        lcd_fill(0, 0, LCD_W - 1, LCD_H - 1, 0xF800);
        if (panic_msg[0]) {
            text_line(0, "PANIC");
            for (int r = 0; r < 3; r++) text_line(1 + r, 20 * r < (int)sizeof panic_msg ? panic_msg + 20 * r : "");
        } else {
            line(0, "FAULT ", ipsr & 0x1FF);
        }
        if (frame) {
            line(5, "PC   ", frame[6]);
            line(6, "LR   ", frame[5]);
        }
        line(7, "CFSR ", SCB_CFSR_);
        line(8, "HFSR ", SCB_HFSR_);
        line(9, "BFAR ", SCB_BFAR);
        line(10, "EXCR ", exc_return);
        text_line(12, "Hold POWER to off");
    }
    uint32_t held = 0;
    for (;;) {
        IWDG_KR = 0xAAAAu;
        if ((REG(GPIOB_BASE + GPIO_IDR) & (1u << 8)) == 0) {
            if (++held > 4000000u) REG(GPIOB_BASE + GPIO_BRR) = (1u << 7);
        } else {
            held = 0;
        }
    }
}

/* I_Error / dmc_fatal: the message is copied, then the same screen (no exception frame) */
void plat_panic(const char *msg)
{
    int n = 0;
    while (msg[n] && n < (int)sizeof panic_msg - 1) { panic_msg[n] = msg[n]; n++; }
    panic_msg[n] = 0;
    __asm volatile("cpsid i");
    fault_screen(0, 0);
    for (;;) {}
}
