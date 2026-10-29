#pragma once
#include <stdint.h>

extern uint32_t sysclk_hz;
extern volatile uint32_t ms_ticks;

/* HSE 8 MHz x9 = 72 MHz exactly as the stock firmware does it (flash latency 2, APB1 /2, USB clock = PLL/1.5).
   Every wait has a timeout; on failure the core stays on HSI 8 MHz and sysclk_hz says so. */
void clock_init(void);
void delay_ms(uint32_t ms);     /* feeds the watchdog while waiting */
