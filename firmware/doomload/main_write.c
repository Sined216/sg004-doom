/*
 * SG-004A NOR loader, WRITE BUILD. Puts the Doom data into the external flash, only above SG_NOR_MIN (sg_layout.h),
 * only after the stock History area still reads exactly like the backup, and only on the W25Q80 it expects.
 *
 *   normal start     the NOR shows up as a USB drive with room for a file; copy doom.uf2 onto it, eject, wait for
 *                    "DONE". Blocks are recognised by their content, FAT housekeeping is ignored.
 *   FN held at start "erase Doom area": erases every sector from SG_NOR_MIN up after a 2 s hold of OK, then checks
 *                    that the whole chip reads like the backup again (CRC32 4A0369D5): the way back to stock.
 *
 * Nothing else is written: no internal flash, no EEPROM, no option bytes, no status register.
 */
#include <stdint.h>
#include <stdio.h>
#include "board.h"
#include "clock.h"
#include "crc32.h"
#include "lcd.h"
#include "msc.h"
#include "nor.h"
#include "nor_write.h"
#include "uf2.h"
#include "usb.h"
#include "vfat.h"
#include "../doom/game/sg_layout.h"

extern int lcd_ready;
extern volatile uint32_t nor_timeouts;

static void gpio_mode(uint32_t port, uint32_t pin, uint32_t mode)
{
    volatile uint32_t *cr = (volatile uint32_t *)(port + (pin < 8 ? GPIO_CRL : GPIO_CRH));
    uint32_t sh = (pin & 7) * 4;
    *cr = (*cr & ~(0xFu << sh)) | (mode << sh);
}

static int gpio_read(uint32_t port, uint32_t pin) { return (REG(port + GPIO_IDR) >> pin) & 1u; }

static void line(int row, const char *s, uint16_t fg)
{
    char b[28];
    int n = 0;
    while (s[n] && n < 26) { b[n] = s[n]; n++; }
    while (n < 26) b[n++] = ' ';
    b[n] = 0;
    lcd_text(0, (uint16_t)(row * 18), b, fg, C_BLACK);
}

void wdt_feed(void) { IWDG_KR = 0xAAAAu; }

static __attribute__((noreturn)) void stop(void)
{
    for (;;) IWDG_KR = 0xAAAAu;             /* the SysTick watchdog still switches the device off on a 2 s POWER hold */
}

static uint32_t crc_range(uint32_t from, uint32_t to)
{
    static uint8_t chunk[4096];
    uint32_t crc = 0;
    for (uint32_t a = from; a < to; a += sizeof chunk) {
        uint32_t n = to - a < sizeof chunk ? to - a : (uint32_t)sizeof chunk;
        nor_read(a, chunk, n);
        crc = crc32_update(crc, chunk, n);
        IWDG_KR = 0xAAAAu;
    }
    return crc;
}

/* What the Doom area (SG_NOR_MIN up) holds now: 0 blank, 1 Doom header, 2 something else (stock History grown that
   far, or half-written data without its header block) */
static uint32_t area_nonblank;                      /* sectors that are not blank */
static int area_state(void)
{
    static uint8_t chunk[256];
    uint8_t magic[8];
    area_nonblank = 0;
    for (uint32_t s = SG_NOR_MIN >> 12; s < (SG_NOR_SIZE >> 12); s++) {
        int blank = 1;
        for (uint32_t o = 0; o < 4096u && blank; o += sizeof chunk) {
            nor_read((s << 12) + o, chunk, sizeof chunk);
            for (uint32_t i = 0; i < sizeof chunk; i++)
                if (chunk[i] != 0xFF) { blank = 0; break; }
        }
        if (!blank) area_nonblank++;
        IWDG_KR = 0xAAAAu;
    }
    if (!area_nonblank) return 0;
    nor_read(SG_NOR_MIN, magic, sizeof magic);
    for (unsigned i = 0; i < sizeof magic; i++)
        if (magic[i] != (uint8_t)SG_MAGIC[i]) return 2;
    return 1;
}

static uint32_t hist_ref;                           /* the History CRC that must still be read at the end */

/* ---------------------------------------------------------------- erase mode */

static void erase_mode(void)
{
    char b[32];
    line(0, "ERASE DOOM AREA", C_RED);
    snprintf(b, sizeof b, "%05lX-%05lX only", (unsigned long)SG_NOR_MIN, (unsigned long)(SG_NOR_SIZE - 1u));
    line(1, b, C_WHITE);
    line(2, "History is not touched", C_GREY);
    line(4, "Hold OK 2 s to erase", C_YELLOW);
    line(5, "POWER 2 s = off", C_GREY);
    uint32_t t0 = 0;
    int held = 0;
    for (;;) {
        IWDG_KR = 0xAAAAu;
        if (!gpio_read(KEY_OK_PORT, KEY_OK_PIN)) {
            if (!held) { held = 1; t0 = ms_ticks; }
            if (ms_ticks - t0 >= 2000u) break;
        } else {
            held = 0;
        }
    }
    uint32_t first = SG_NOR_MIN >> 12, last = (SG_NOR_SIZE >> 12) - 1u;
    for (uint32_t s = first; s <= last; s++) {
        int r = nor_wr_erase4k(s << 12);
        if (r) {
            snprintf(b, sizeof b, "ERROR %d sector %lu", r, (unsigned long)s);
            line(7, b, C_RED);
            stop();
        }
        if (!((s - first) & 7u)) {
            snprintf(b, sizeof b, "erased %lu/%lu", (unsigned long)(s - first + 1u), (unsigned long)(last - first + 1u));
            line(7, b, C_WHITE);
        }
    }
    uint32_t chip = crc_range(0, SG_NOR_SIZE);
    snprintf(b, sizeof b, "chip CRC32 %08lX", (unsigned long)chip);
    line(8, b, C_WHITE);
    if (!SG_BACKUP_CHIP_CRC) {
        uint32_t h = crc_range(0, SG_NOR_MIN);
        line(9, h == hist_ref ? "History unchanged" : "HISTORY CHANGED!", h == hist_ref ? C_GREEN : C_RED);
        line(10, "Compare with your backup", C_GREY);
    } else if (chip == SG_BACKUP_CHIP_CRC) {
        line(9, "= backup: back to stock", C_GREEN);
    } else {
        line(9, "differs from the backup", C_RED);
        line(10, "(History changed since?)", C_RED);
    }
    stop();
}

/* ---------------------------------------------------------------- write mode */

int main(void)
{
    RCC_APB2ENR |= (1u << 0) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6);
    REG(GPIOB_BASE + GPIO_BSRR) = 1u << PWR_HOLD_PIN;
    gpio_mode(PWR_HOLD_PORT, PWR_HOLD_PIN, GM_OUT_PP_2M);
    AFIO_MAPR = (AFIO_MAPR & ~(7u << 24)) | (2u << 24);
    REG(PWR_KEY_PORT + GPIO_BSRR) = 1u << PWR_KEY_PIN;
    gpio_mode(PWR_KEY_PORT, PWR_KEY_PIN, GM_IN_PULL);
    REG(KEY_FN_PORT + GPIO_BSRR) = 1u << KEY_FN_PIN;
    gpio_mode(KEY_FN_PORT, KEY_FN_PIN, GM_IN_PULL);
    REG(KEY_OK_PORT + GPIO_BSRR) = 1u << KEY_OK_PIN;
    gpio_mode(KEY_OK_PORT, KEY_OK_PIN, GM_IN_PULL);

    for (int i = 0; i < 3; i++) {
        REG(0xE000E180u + 4u * i) = 0xFFFFFFFFu;    /* NVIC_ICER */
        REG(0xE000E280u + 4u * i) = 0xFFFFFFFFu;    /* NVIC_ICPR */
    }
    usb_early_detach();

    REG(BL_PORT + GPIO_BSRR) = 1u << BL_PIN;
    gpio_mode(BL_PORT, BL_PIN, GM_OUT_PP_2M);
    for (volatile int i = 0; i < 1000; i++) {}
    int erase = !gpio_read(KEY_FN_PORT, KEY_FN_PIN);        /* FN held at power-on */

    clock_init();
    lcd_bus_init(sysclk_hz);
    lcd_init();
    lcd_fill(0, 0, LCD_W, LCD_H, C_BLACK);
    REG(BL_PORT + GPIO_BRR) = 1u << BL_PIN;
    lcd_ready = 1;

    char b[32];
    line(0, erase ? "ERASE DOOM AREA" : "DOOM DATA LOADER (WRITE)", erase ? C_RED : C_YELLOW);
    if (sysclk_hz != 72000000u) {
        line(2, "NO 72 MHZ CLOCK: STOP", C_RED);
        stop();
    }

    /* the preconditions for writing, enforced here: the expected chip, no protection bits, History as expected */
    nor_init();
    struct nor_wr_status st;
    int pr = nor_wr_probe(&st);
    snprintf(b, sizeof b, "JEDEC %06lX SR %02X %02X", (unsigned long)st.jedec, st.sr1, st.sr2);
    line(1, b, pr == NW_OK ? C_WHITE : C_RED);
    if (pr == NW_BAD_CHIP) { line(3, "NOT THE EXPECTED W25Q80", C_RED); stop(); }
    if (pr == NW_PROTECTED) { line(3, "CHIP WRITE PROTECTED", C_RED); line(4, "not changed by this tool", C_GREY); stop(); }
    uint32_t hist = crc_range(0, SG_NOR_MIN);
    snprintf(b, sizeof b, "History CRC %08lX", (unsigned long)hist);
    if (SG_HIST_CRC && hist != SG_HIST_CRC) {
        line(2, b, C_RED);
        line(3, "DIFFERS FROM THE BACKUP", C_RED);
        line(4, "Make a new backup first", C_WHITE);
        line(5, "(NORBACKUP loader)", C_GREY);
        stop();
    }
    hist_ref = SG_HIST_CRC ? SG_HIST_CRC : hist;
    line(2, b, SG_HIST_CRC ? C_GREEN : C_YELLOW);
    if (!SG_HIST_CRC) line(11, "No backup CRC in this build", C_YELLOW);

    int area = area_state();
    if (!erase && area == 2 && !SG_HIST_CRC) {
        line(3, "AREA NOT BLANK, NOT DOOM", C_RED);
        snprintf(b, sizeof b, "%lu sectors from %05lX", (unsigned long)area_nonblank, (unsigned long)SG_NOR_MIN);
        line(4, b, C_WHITE);
        line(5, "History may reach here:", C_WHITE);
        line(6, "back up, then FN at start", C_GREY);
        stop();
    }

    if (erase) {
        erase_mode();
        stop();
    }

    snprintf(b, sizeof b, "Area %05lX-%05lX only", (unsigned long)SG_NOR_MIN, (unsigned long)(SG_NOR_SIZE - 1u));
    line(3, b, C_GREY);

    uf2_reset();
    msc_write_hook = uf2_block;
    vfat_init_blank(16384);                         /* 8 MB of free clusters: the UF2 file is twice the data */
    static const char info[] = "SG-004A Doom data loader (write)\r\nCopy doom.uf2 to this drive, eject, wait for DONE\r\n";
    vfat_set_info(info, sizeof info - 1);
    msc_init();
    usb_init();

    uint32_t t_status = ms_ticks;
    int finished = 0;
    uint32_t data_crc = 0, hist_after = 0;
    for (;;) {
        IWDG_KR = 0xAAAAu;
        msc_poll();

        if (!finished && uf2_complete()) {
            finished = 1;                           /* every page received and verified: check the result as a whole */
            data_crc = crc_range(SG_NOR_MIN, SG_NOR_MIN + uf2.total * 256u);
            hist_after = crc_range(0, SG_NOR_MIN);
        }

        if (ms_ticks - t_status >= 500u) {
            t_status = ms_ticks;
            if (finished) {
                line(4, hist_after == hist_ref ? "DONE: verified" : "DONE, HISTORY CHANGED!",
                     hist_after == hist_ref ? C_GREEN : C_RED);
                snprintf(b, sizeof b, "data CRC32 %08lX", (unsigned long)data_crc);
                line(5, b, C_GREEN);
                line(6, hist_after == hist_ref ? "History unchanged" : "History differs", hist_after == hist_ref ? C_GREEN : C_RED);
                line(7, "Eject the drive, power off", C_WHITE);
            } else {
                line(4, uf2.total ? "writing..." : "waiting for doom.uf2", C_WHITE);
                snprintf(b, sizeof b, "pages %lu/%lu", (unsigned long)uf2.ok, (unsigned long)uf2.total);
                line(5, b, C_WHITE);
                snprintf(b, sizeof b, "dup %lu mism %lu rej %lu", (unsigned long)uf2.dup, (unsigned long)uf2.mismatch, (unsigned long)uf2.rejected);
                line(6, b, (uf2.mismatch || uf2.rejected) ? C_RED : C_GREY);
                snprintf(b, sizeof b, "err %lu (%d) ign %lu", (unsigned long)uf2.errors, uf2.last_err, (unsigned long)uf2.ignored);
                line(7, b, uf2.errors ? C_RED : C_GREY);
            }
            snprintf(b, sizeof b, "erased %lu sectors", (unsigned long)uf2.erased);
            line(8, b, C_GREY);
            snprintf(b, sizeof b, "USB %s rst %lu set %lu", usb_configured() ? "READY" : "waiting", (unsigned long)usb_dbg.resets, (unsigned long)usb_dbg.setups);
            line(9, b, usb_configured() ? C_GREEN : C_GREY);
            snprintf(b, sizeof b, "cmds %lu nor_to %lu", (unsigned long)msc_cmds, (unsigned long)nor_timeouts);
            line(10, b, C_GREY);
            line(12, "POWER 2 s = off", C_GREY);
        }
    }
}
