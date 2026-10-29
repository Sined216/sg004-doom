#pragma once
#include <stdint.h>

enum clk_src { CLK_HSI = 0, CLK_PLL_HSE, CLK_PLL_HSI };

extern uint32_t sysclk_hz;
extern enum clk_src clk_src;
extern volatile uint32_t ms_ticks;

void clock_init(int fast);      /* fast: 240 MHz from PLL, falling back to HSI 8 MHz if anything does not start; else HSI 8 MHz */
void delay_ms(uint32_t ms);     /* feeds the watchdog while waiting */
