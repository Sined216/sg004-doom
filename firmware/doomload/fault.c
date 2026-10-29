/*
 * Fatal fault screen: red background with the exception number, stacked PC/LR and fault status
 * registers, so a crash on the device can be located in build/doom.map. The power key keeps working.
 */
#include <stdint.h>
#include <stdio.h>
#include "board.h"
#include "lcd.h"

#define SCB_MMFAR REG(0xE000ED34u)
#define SCB_BFAR  REG(0xE000ED38u)

int lcd_ready;

static void line(int row, const char *s)
{
    lcd_text(0, (uint16_t)(row * 18), s, C_WHITE, C_RED);
}

void fault_screen(const uint32_t *frame, uint32_t exc_return)
{
    uint32_t ipsr;
    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    REG(GPIOC_BASE + GPIO_BRR) = 1u << 6;             /* backlight on */
    if (lcd_ready) {
        char buf[32];
        lcd_fill(0, 0, LCD_W, LCD_H, C_RED);
        snprintf(buf, sizeof buf, "FAULT exc %lu", (unsigned long)(ipsr & 0x1FF));   line(0, buf);
        snprintf(buf, sizeof buf, "PC   %08lX", (unsigned long)frame[6]);           line(1, buf);
        snprintf(buf, sizeof buf, "LR   %08lX", (unsigned long)frame[5]);           line(2, buf);
        snprintf(buf, sizeof buf, "CFSR %08lX", (unsigned long)SCB_CFSR);           line(3, buf);
        snprintf(buf, sizeof buf, "HFSR %08lX", (unsigned long)SCB_HFSR);           line(4, buf);
        snprintf(buf, sizeof buf, "BFAR %08lX", (unsigned long)SCB_BFAR);           line(5, buf);
        snprintf(buf, sizeof buf, "MMAR %08lX", (unsigned long)SCB_MMFAR);          line(6, buf);
        snprintf(buf, sizeof buf, "SP   %08lX", (unsigned long)(uintptr_t)frame);   line(7, buf);
        snprintf(buf, sizeof buf, "EXCR %08lX", (unsigned long)exc_return);         line(8, buf);
        line(10, "Hold POWER to switch off");
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
