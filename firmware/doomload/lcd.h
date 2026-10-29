#pragma once
#include <stdint.h>

#define LCD_W 320
#define LCD_H 240

#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_YELLOW 0xFFE0
#define C_GREEN  0x07E0
#define C_RED    0xF800
#define C_GREY   0x8410
#define C_BLUE   0x001F

void lcd_bus_init(uint32_t hclk_hz);
void lcd_init(void);
void lcd_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c);
/* 5x7 font scaled x2 in a 12x16 cell, pixel coordinates */
void lcd_text(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg);
/* 8-bit indexed image, each pixel doubled in both directions, top-left at (x, y) */
void lcd_blit2x(uint16_t x, uint16_t y, const uint8_t *src, uint16_t w, uint16_t h, const uint16_t *pal);
