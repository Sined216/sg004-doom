/* ST7789 320x240 over FSMC/XMC bank 1 NE1, 16-bit, RS = A16 (init sequence copied from stock firmware) */
#include "board.h"
#include "clock.h"
#include "lcd.h"
#include "font5x7.h"

/* MADCTL for landscape; 0x60 showed the image upside down on the real device */
#define LCD_MADCTL 0xA0

static void gpio_af(uint32_t port, uint32_t pin)
{
    volatile uint32_t *cr = (volatile uint32_t *)(port + (pin < 8 ? GPIO_CRL : GPIO_CRH));
    uint32_t sh = (pin & 7) * 4;
    *cr = (*cr & ~(0xFu << sh)) | (GM_AF_PP_50M << sh);
}

static void lcd_cmd(uint16_t c)  { LCD_CMD = c; }
static void lcd_dat(uint16_t d)  { LCD_DATA = d; }

void lcd_bus_init(uint32_t hclk_hz)
{
    static const uint8_t pd_pins[] = {0, 1, 4, 5, 7, 8, 9, 10, 11, 14, 15};
    RCC_AHBENR |= 1u << 8;                          /* FSMC/XMC clock */
    for (unsigned i = 0; i < sizeof pd_pins; i++) gpio_af(GPIOD_BASE, pd_pins[i]);
    for (unsigned p = 7; p <= 15; p++) gpio_af(GPIOE_BASE, p);

    REG(LCD_RST_PORT + GPIO_BSRR) = 1u << LCD_RST_PIN;
    volatile uint32_t *cr = (volatile uint32_t *)(LCD_RST_PORT + GPIO_CRL);
    *cr = (*cr & ~(0xFu << (LCD_RST_PIN * 4))) | (GM_OUT_PP_2M << (LCD_RST_PIN * 4));

    /* ST7789 write cycle >= 66 ns. 72 MHz HCLK is 13.9 ns: (ADDSET+1) + (DATAST+1) = 3 + 7 = 10 clocks = 139 ns.
       At 8 MHz the diag timing (1/1/2) is already slow. */
    if (hclk_hz > 50000000u)
        FSMC_BTR1 = (6u << 8) | (1u << 4) | 2u;
    else
        FSMC_BTR1 = (2u << 8) | (1u << 4) | 1u;
    FSMC_BCR1 = (1u << 12) | (1u << 4) | 1u;        /* write enable, 16-bit, SRAM, bank on */
}

void lcd_init(void)
{
    static const uint8_t seq[] = {
        /* cmd, n, data... */
        0x36, 1, LCD_MADCTL,
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

    REG(LCD_RST_PORT + GPIO_BSRR) = 1u << LCD_RST_PIN;  delay_ms(10);
    REG(LCD_RST_PORT + GPIO_BRR)  = 1u << LCD_RST_PIN;  delay_ms(20);
    REG(LCD_RST_PORT + GPIO_BSRR) = 1u << LCD_RST_PIN;  delay_ms(20);

    lcd_cmd(0x11);
    delay_ms(120);
    for (unsigned i = 0; i < sizeof seq; ) {
        uint8_t c = seq[i++], n = seq[i++];
        lcd_cmd(c);
        while (n--) lcd_dat(seq[i++]);
    }
    lcd_cmd(0x29);
}

static void lcd_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    lcd_cmd(0x2A); lcd_dat(x >> 8); lcd_dat(x & 0xFF); lcd_dat((x + w - 1) >> 8); lcd_dat((x + w - 1) & 0xFF);
    lcd_cmd(0x2B); lcd_dat(y >> 8); lcd_dat(y & 0xFF); lcd_dat((y + h - 1) >> 8); lcd_dat((y + h - 1) & 0xFF);
    lcd_cmd(0x2C);
}

void lcd_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    lcd_window(x, y, w, h);
    for (uint32_t n = (uint32_t)w * h; n; n--) lcd_dat(c);
}

#define CW 12
#define CH 16

static void draw_char(uint16_t x, uint16_t y, char ch, uint16_t fg, uint16_t bg)
{
    const uint8_t *g = font5x7[(ch < 0x20 || ch > 0x7E) ? 0 : ch - 0x20];
    lcd_window(x, y, CW, CH);
    for (int row = 0; row < CH; row++) {
        int r = row / 2;
        for (int col = 0; col < CW; col++) {
            int c = col / 2;
            lcd_dat((c < 5 && r < 7 && (g[c] >> r) & 1) ? fg : bg);
        }
    }
}

void lcd_text(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg)
{
    for (; *s && x + CW <= LCD_W; s++, x += CW) draw_char(x, y, *s, fg, bg);
}

void lcd_blit2x(uint16_t x, uint16_t y, const uint8_t *src, uint16_t w, uint16_t h, const uint16_t *pal)
{
    lcd_window(x, y, w * 2, h * 2);
    for (uint16_t r = 0; r < h; r++, src += w) {
        for (int rep = 0; rep < 2; rep++) {
            const uint8_t *p = src;
            for (uint16_t n = w; n; n--) {
                uint16_t c = pal[*p++];
                LCD_DATA = c;
                LCD_DATA = c;
            }
        }
    }
}
