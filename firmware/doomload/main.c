/*
 * SG-004A NOR backup loader, read-only build.
 * Shows the external SPI NOR flash as a USB drive with NOR.BIN (the whole chip) and INFO.TXT (size, CRC32),
 * so the stock History can be copied to the PC and verified before anything is ever written to it.
 * This build has no code that programs, erases or write-enables any flash (internal or external) or touches
 * the EEPROM or option bytes. Analog-stage pins stay in their reset state.
 */
#include <stdint.h>
#include <stdio.h>
#include "board.h"
#include "clock.h"
#include "crc32.h"
#include "lcd.h"
#include "msc.h"
#include "nor.h"
#include "usb.h"
#include "vfat.h"

extern int lcd_ready;
extern volatile uint32_t nor_timeouts;

static void gpio_mode(uint32_t port, uint32_t pin, uint32_t mode)
{
    volatile uint32_t *cr = (volatile uint32_t *)(port + (pin < 8 ? GPIO_CRL : GPIO_CRH));
    uint32_t sh = (pin & 7) * 4;
    *cr = (*cr & ~(0xFu << sh)) | (mode << sh);
}

static void gpio_set(uint32_t port, uint32_t pin)   { REG(port + GPIO_BSRR) = 1u << pin; }
static void gpio_clr(uint32_t port, uint32_t pin)   { REG(port + GPIO_BRR) = 1u << pin; }
static int  gpio_read(uint32_t port, uint32_t pin)  { return (REG(port + GPIO_IDR) >> pin) & 1u; }

static void line(int row, const char *s, uint16_t fg)
{
    char b[28];
    int n = 0;
    while (s[n] && n < 26) { b[n] = s[n]; n++; }
    while (n < 26) b[n++] = ' ';
    b[n] = 0;
    lcd_text(0, (uint16_t)(row * 18), b, fg, C_BLACK);
}

static void power_off(void)
{
    lcd_fill(0, 0, LCD_W, LCD_H, C_BLACK);
    gpio_set(BL_PORT, BL_PIN);                      /* backlight off */
    gpio_clr(PWR_HOLD_PORT, PWR_HOLD_PIN);          /* drop the power latch; power goes once the key is released */
    for (;;) IWDG_KR = 0xAAAAu;
}

/* waits for a 2 s hold of the power key; used on error screens */
static void wait_power_off(void)
{
    uint32_t low_since = ms_ticks;
    int released = 0;
    for (;;) {
        IWDG_KR = 0xAAAAu;
        if (gpio_read(PWR_KEY_PORT, PWR_KEY_PIN)) { released = 1; low_since = ms_ticks; }
        else if (released && ms_ticks - low_since >= 2000u) power_off();
    }
}

static void nor_rd(uint32_t off, uint8_t *dst, uint32_t n) { nor_read(off, dst, n); }

int main(void)
{
    /* Hold power first, before anything that could take time */
    RCC_APB2ENR |= (1u << 0) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6);
    gpio_set(PWR_HOLD_PORT, PWR_HOLD_PIN);
    gpio_mode(PWR_HOLD_PORT, PWR_HOLD_PIN, GM_OUT_PP_2M);
    AFIO_MAPR = (AFIO_MAPR & ~(7u << 24)) | (2u << 24);   /* JTAG off (frees PB3/PB4), SWD stays on */
    gpio_set(PWR_KEY_PORT, PWR_KEY_PIN);
    gpio_mode(PWR_KEY_PORT, PWR_KEY_PIN, GM_IN_PULL);

    /* the bootloader may have left peripheral interrupts enabled; this build polls and uses only SysTick */
    for (int i = 0; i < 3; i++) {
        REG(0xE000E180u + 4u * i) = 0xFFFFFFFFu;    /* NVIC_ICER */
        REG(0xE000E280u + 4u * i) = 0xFFFFFFFFu;    /* NVIC_ICPR */
    }

    /* The bootloader was an enumerated USB device a moment ago: put the USB block back to reset, powered down,
       and hold D+/D- low so the host sees a detach for the whole start-up (the CRC pass takes seconds) */
    usb_early_detach();

    gpio_set(BL_PORT, BL_PIN);
    gpio_mode(BL_PORT, BL_PIN, GM_OUT_PP_2M);

    clock_init();
    lcd_bus_init(sysclk_hz);
    lcd_init();
    lcd_fill(0, 0, LCD_W, LCD_H, C_BLACK);
    gpio_clr(BL_PORT, BL_PIN);                      /* backlight on (active low) */
    lcd_ready = 1;

    char b[32];
    line(0, "NOR BACKUP loader v0.2 RO", C_YELLOW);
    snprintf(b, sizeof b, "%luMHz CFGR %08lX", (unsigned long)(sysclk_hz / 1000000u), (unsigned long)RCC_CFGR);
    line(1, b, sysclk_hz == 72000000u ? C_WHITE : C_RED);
    if (sysclk_hz != 72000000u) {
        line(3, "NO 72 MHZ CLOCK: USB OFF", C_RED);
        line(5, "Hold POWER 2 s = off", C_GREY);
        wait_power_off();
    }

    nor_init();
    struct nor_id id;
    int ok = nor_identify(&id);
    snprintf(b, sizeof b, "JEDEC %02X %02X %02X", id.mfr, id.type, id.cap);
    line(2, b, C_WHITE);
    if (ok < 0 || vfat_init(id.size, nor_rd) < 0) {
        line(4, "NO SUPPORTED FLASH FOUND", C_RED);
        line(5, "USB not started", C_RED);
        line(7, "Hold POWER 2 s = off", C_GREY);
        wait_power_off();
    }
    snprintf(b, sizeof b, "size %lu KiB", (unsigned long)(id.size / 1024u));
    line(3, b, C_WHITE);

    /* CRC32 of the whole chip: shown here and written to INFO.TXT, to verify the copy made over USB */
    uint32_t crc = 0;
    static uint8_t chunk[4096];
    for (uint32_t off = 0; off < id.size; off += sizeof chunk) {
        nor_read(off, chunk, sizeof chunk);
        crc = crc32_update(crc, chunk, sizeof chunk);
        IWDG_KR = 0xAAAAu;
        if ((off & 0x3FFFFu) == 0) {
            snprintf(b, sizeof b, "reading... %lu%%", (unsigned long)(off * 100u / id.size));
            line(4, b, C_GREY);
        }
    }
    snprintf(b, sizeof b, "CRC32 %08lX", (unsigned long)crc);
    line(4, b, C_GREEN);

    char info[160];
    int n = snprintf(info, sizeof info, "SG-004A NOR backup (read-only)\r\nJEDEC %02X %02X %02X\r\nSIZE %lu\r\nCRC32 %08lX\r\n",
                     id.mfr, id.type, id.cap, (unsigned long)id.size, (unsigned long)crc);
    vfat_set_info(info, (uint32_t)n);

    msc_init();
    usb_init();
    line(5, "Connect USB, copy NOR.BIN", C_WHITE);
    line(6, "POWER 2 s = off", C_GREY);

    uint32_t t_status = ms_ticks;
    for (;;) {
        IWDG_KR = 0xAAAAu;
        msc_poll();

        if (ms_ticks - t_status >= 500u) {
            uint32_t daddr, ep0r, istr;
            t_status = ms_ticks;
            snprintf(b, sizeof b, "USB %s", usb_configured() ? "READY" : "waiting");
            line(7, b, usb_configured() ? C_GREEN : C_GREY);
            snprintf(b, sizeof b, "cmds %lu blk %lu", (unsigned long)msc_cmds, (unsigned long)msc_reads);
            line(8, b, C_WHITE);
            snprintf(b, sizeof b, "writes refused %lu", (unsigned long)msc_writes_refused);
            line(9, b, C_GREY);
            snprintf(b, sizeof b, "rst %lu setup %lu nor_to %lu", (unsigned long)usb_dbg.resets,
                     (unsigned long)usb_dbg.setups, (unsigned long)nor_timeouts);
            line(10, b, C_WHITE);
            snprintf(b, sizeof b, "%02X %02X %02X %02X %02X %02X %02X %02X", usb_dbg.last_setup[0], usb_dbg.last_setup[1],
                     usb_dbg.last_setup[2], usb_dbg.last_setup[3], usb_dbg.last_setup[4], usb_dbg.last_setup[5],
                     usb_dbg.last_setup[6], usb_dbg.last_setup[7]);
            line(11, b, C_GREY);
            usb_dbg_regs(&daddr, &ep0r, &istr);
            snprintf(b, sizeof b, "A%02lX E%04lX I%04lX", (unsigned long)daddr, (unsigned long)ep0r, (unsigned long)istr);
            line(12, b, C_GREY);
        }
    }
}
