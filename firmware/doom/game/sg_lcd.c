/*
 * LCD driver of the SG-004A for the DMC100 engine (interface of dmc100/platform/lcd.h): ST7789 320x240 on FSMC bank 1,
 * 16-bit bus, RS = A16 (command 0x60000000, data 0x60020000), landscape with MADCTL 0xA0 (confirmed upright on the
 * device).
 *
 * The pump is polled and needs no DMA: a submitted strip is a list of 8-bit columns, each is sent as its own window
 * of width 1 (or 2 in low detail, every pixel twice) and the column's height, so the orientation stays the one
 * that is already known to be right and does not depend on a column-major MADCTL.
 */
#include "board.h"
#include "clock.h"
#include "lcd.h"
#include "platform.h"

uint16_t pump_lut[256];
volatile struct pump_stats pump_stats;

int lcd_ready;

static int flipped, profile_id;
static int pump_open, pump_x, pump_x1, pump_y0, pump_y1, pump_reps;

static void lcd_cmd(uint16_t c) { LCD_CMD = c; }
static void lcd_dat(uint16_t d) { LCD_DATA = d; }

static void gpio_af(uint32_t port, uint32_t pin)
{
    volatile uint32_t *cr = (volatile uint32_t *)(port + (pin < 8 ? GPIO_CRL : GPIO_CRH));
    uint32_t sh = (pin & 7) * 4;
    *cr = (*cr & ~(0xFu << sh)) | (GM_AF_PP_50M << sh);
}

static void bus_init(void)
{
    static const uint8_t pd_pins[] = {0, 1, 4, 5, 7, 8, 9, 10, 11, 14, 15};
    RCC_AHBENR |= 1u << 8;                          /* FSMC/XMC clock */
    for (unsigned i = 0; i < sizeof pd_pins; i++) gpio_af(GPIOD_BASE, pd_pins[i]);
    for (unsigned p = 7; p <= 15; p++) gpio_af(GPIOE_BASE, p);

    REG(LCD_RST_PORT + GPIO_BSRR) = 1u << LCD_RST_PIN;
    volatile uint32_t *cr = (volatile uint32_t *)(LCD_RST_PORT + GPIO_CRL);
    *cr = (*cr & ~(0xFu << (LCD_RST_PIN * 4))) | (GM_OUT_PP_2M << (LCD_RST_PIN * 4));

    /* ST7789 write cycle >= 66 ns; at 240 MHz one HCLK is 4.2 ns: ADDSET 4 + DATAST 12 + 1 = 17 clocks = 71 ns */
    if (sysclk_hz > 100000000u)
        FSMC_BTR1 = (12u << 8) | (1u << 4) | 4u;
    else
        FSMC_BTR1 = (2u << 8) | (1u << 4) | 1u;
    FSMC_BCR1 = (1u << 12) | (1u << 4) | 1u;        /* write enable, 16-bit, SRAM, bank on */
}

void lcd_window(int x0, int y0, int x1, int y1)
{
    lcd_cmd(0x2A); lcd_dat((uint16_t)(x0 >> 8)); lcd_dat((uint16_t)(x0 & 0xFF)); lcd_dat((uint16_t)(x1 >> 8)); lcd_dat((uint16_t)(x1 & 0xFF));
    lcd_cmd(0x2B); lcd_dat((uint16_t)(y0 >> 8)); lcd_dat((uint16_t)(y0 & 0xFF)); lcd_dat((uint16_t)(y1 >> 8)); lcd_dat((uint16_t)(y1 & 0xFF));
    lcd_cmd(0x2C);
}

void lcd_init(int prof)
{
    static const uint8_t seq[] = {
        /* cmd, n, data... ; copied from the stock firmware */
        0x3A, 1, 0x05,
        0xB2, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33,
        0xB7, 1, 0x35,
        0xBB, 1, 0x28,
        0xC0, 1, 0x2C,
        0xC2, 1, 0x01,
        0xC3, 1, 0x0B,
        0xC4, 1, 0x20,
        0xC6, 1, 0x0F,
        0xD0, 2, 0xA4, 0xA1,
        0xE0, 14, 0xD0, 0x01, 0x08, 0x0F, 0x11, 0x2A, 0x36, 0x55, 0x44, 0x3A, 0x0B, 0x06, 0x11, 0x20,
        0xE1, 14, 0xD0, 0x02, 0x07, 0x0A, 0x0B, 0x18, 0x34, 0x43, 0x4A, 0x2B, 0x1B, 0x1C, 0x22, 0x1F,
        0x21, 0,
    };
    profile_id = prof;
    bus_init();

    REG(LCD_RST_PORT + GPIO_BSRR) = 1u << LCD_RST_PIN;  delay_ms(10);
    REG(LCD_RST_PORT + GPIO_BRR)  = 1u << LCD_RST_PIN;  delay_ms(20);
    REG(LCD_RST_PORT + GPIO_BSRR) = 1u << LCD_RST_PIN;  delay_ms(20);

    lcd_cmd(0x11);
    delay_ms(120);
    lcd_cmd(0x36); lcd_dat(0xA0);                   /* landscape, upright on the device */
    for (unsigned i = 0; i < sizeof seq; ) {
        uint8_t c = seq[i++], n = seq[i++];
        lcd_cmd(c);
        while (n--) lcd_dat(seq[i++]);
    }
    lcd_cmd(0x29);
    flipped = 0;
    lcd_ready = 1;
}

int lcd_profile(void) { return profile_id; }

void lcd_flip(int flip)
{
    flipped = flip != 0;
    lcd_cmd(0x36);
    lcd_dat(flipped ? 0x60 : 0xA0);
}

int lcd_flipped(void) { return flipped; }

void lcd_backlight(int on)
{
    if (on) REG(BL_PORT + GPIO_BRR) = 1u << BL_PIN;     /* active low */
    else    REG(BL_PORT + GPIO_BSRR) = 1u << BL_PIN;
}

/* The lcd.h contract (platform/text.c): the pixels of a window arrive column by column, top to bottom (the DMC100
   panel is mounted that way). This panel is written row by row, so a small window is collected and transposed in
   lcd_end. Windows are at most 32x32 pixels (8x8 text at scale 4); larger fills use lcd_fill. */
#define BEGIN_MAX 1024u
static uint16_t wbuf[BEGIN_MAX];
static uint32_t wcount;
static int wx0, wy0, wx1, wy1;

void lcd_begin(int x0, int y0, int x1, int y1)
{
    wx0 = x0; wy0 = y0; wx1 = x1; wy1 = y1;
    wcount = 0;
}

void lcd_push(uint16_t c)
{
    if (wcount < BEGIN_MAX) wbuf[wcount++] = c;
}

void lcd_push_n(const uint16_t *px, uint32_t n)
{
    while (n--) lcd_push(*px++);
}

void lcd_end(void)
{
    const uint32_t w = (uint32_t)(wx1 - wx0 + 1), h = (uint32_t)(wy1 - wy0 + 1);
    if (!wcount || wcount != w * h) { wcount = 0; return; }
    lcd_window(wx0, wy0, wx1, wy1);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
            LCD_DATA = wbuf[x * h + y];
    wcount = 0;
}

void lcd_fill(int x0, int y0, int x1, int y1, uint16_t c)
{
    lcd_window(x0, y0, x1, y1);
    for (uint32_t n = (uint32_t)(x1 - x0 + 1) * (uint32_t)(y1 - y0 + 1); n; n--) LCD_DATA = c;
}

void lcd_recover(void) {}

/* ---------------------------------------------------------------- pump */

void pump_begin(int x0, int x1, int y0, int y1, int flags)
{
    pump_x = x0;
    pump_x1 = x1;
    pump_y0 = y0;
    pump_y1 = y1;
    pump_reps = (flags & PUMP_DOUBLE) ? 2 : 1;
    pump_open = 1;
}

void pump_submit(const uint8_t *cols, int ncols)
{
    if (!pump_open || ncols <= 0) return;
    const uint32_t h = (uint32_t)(pump_y1 - pump_y0 + 1);
    const uint16_t *lut = pump_lut;
    for (; ncols > 0; ncols--, cols += h) {
        if (pump_x > pump_x1) break;
        lcd_window(pump_x, pump_y0, pump_x + pump_reps - 1, pump_y1);
        if (pump_reps == 2) {
            for (uint32_t i = 0; i < h; i++) {
                uint16_t c = lut[cols[i]];
                LCD_DATA = c;
                LCD_DATA = c;
            }
        } else {
            for (uint32_t i = 0; i < h; i++) LCD_DATA = lut[cols[i]];
        }
        pump_x += pump_reps;
        pump_stats.fills += h;
    }
}

void pump_sync(int max) { (void)max; }
int pump_pending(void) { return 0; }
void pump_end(void) { pump_open = 0; }
void pump_close(void) { pump_open = 0; }
void pump_reset(void) { pump_open = 0; }
