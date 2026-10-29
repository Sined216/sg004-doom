/*
 * AT32F403A clock: HEXT 8 MHz x30 = 240 MHz (the part's rated maximum; stock firmware only runs 72 MHz).
 * Sequence follows Artery's BSP: PLL range bit for >72 MHz, AHB /1, APB1/APB2 /2 (120 MHz max),
 * and the CRM auto-step mode while switching SYSCLK so the core current ramps gradually.
 * Every wait has a timeout; on any failure the core stays on HSI 8 MHz so the device stays controllable.
 */
#include "board.h"
#include "clock.h"

uint32_t sysclk_hz = 8000000;
enum clk_src clk_src = CLK_HSI;
volatile uint32_t ms_ticks;

#define CR_HSEON    (1u << 16)
#define CR_HSERDY   (1u << 17)
#define CR_PLLON    (1u << 24)
#define CR_PLLRDY   (1u << 25)

/* CFGR fields; PLL multiplier code is (mult - 1) for mult >= 17, low 4 bits at 21:18, high 2 bits at 30:29 */
#define CFGR_PLL_MASK   ((1u << 16) | (1u << 17) | (0xFu << 18) | (3u << 29) | (1u << 31))
#define CFGR_PLLSRC_HSE (1u << 16)
#define CFGR_PLLRANGE   (1u << 31)
#define CFGR_PLLMUL(m)  (((((m) - 1u) & 0xFu) << 18) | ((((m) - 1u) >> 4) << 29))

/* ~100k loop iterations is several tens of ms at 8 MHz, far longer than HSE/PLL need */
static int wait_set(volatile uint32_t *reg, uint32_t mask, uint32_t want)
{
    for (uint32_t n = 0; n < 100000u; n++) {
        if ((*reg & mask) == want) return 1;
        IWDG_KR = 0xAAAAu;
    }
    return 0;
}

static void systick_start(void)
{
    SYST_CSR = 0;
    SYST_RVR = sysclk_hz / 1000u - 1u;
    SYST_CVR = 0;
    SYST_CSR = 7u;                                  /* core clock, interrupt, enable */
}

/* Per-millisecond hook of the input driver (sg_input.c) */
void sg_input_sample(void);

/* Power-key watchdog: a 2 s hold of POWER switches the device off from here, so that a hang anywhere in the
   game (SPI, a loop in the engine) can never leave PB7 latched with no way out. The key must be released once
   first, because it is still held while the device starts. */
void SysTick_Handler(void)
{
    static uint8_t released;
    static uint32_t low_ms;
    ms_ticks++;
    if (REG(GPIOB_BASE + GPIO_IDR) & (1u << 8)) {
        released = 1;
        low_ms = 0;
    } else if (released && ++low_ms >= 2000u) {
        REG(GPIOC_BASE + GPIO_BSRR) = 1u << 6;          /* backlight off */
        REG(GPIOB_BASE + GPIO_BRR) = 1u << 7;           /* drop the power latch */
    }
    sg_input_sample();
}

static void fall_back_to_hsi(void)
{
    RCC_CFGR &= ~3u;                                /* SYSCLK = HSI */
    wait_set(&RCC_CFGR, 0xCu, 0);
    RCC_CR &= ~CR_PLLON;
    RCC_MISC2 &= ~(3u << 4);
    sysclk_hz = 8000000;
    clk_src = CLK_HSI;
}

void clock_init(int fast)
{
    /* Start from a known state: HSI as SYSCLK, PLL off (the bootloader may have left it on) */
    RCC_CR |= 1u;
    wait_set(&RCC_CR, 2u, 2u);
    RCC_CFGR &= ~3u;
    wait_set(&RCC_CFGR, 0xCu, 0);
    RCC_CR &= ~CR_PLLON;
    wait_set(&RCC_CR, CR_PLLRDY, 0);

    if (!fast) {
        systick_start();
        return;
    }

    uint32_t pll;
    RCC_CR |= CR_HSEON;
    if (wait_set(&RCC_CR, CR_HSERDY, CR_HSERDY)) {
        pll = CFGR_PLLSRC_HSE | CFGR_PLLMUL(30);    /* 8 MHz x30 */
        clk_src = CLK_PLL_HSE;
    } else {
        RCC_CR &= ~CR_HSEON;
        pll = CFGR_PLLMUL(60);                      /* HSI/2 = 4 MHz x60 */
        clk_src = CLK_PLL_HSI;
    }

    uint32_t cfgr = RCC_CFGR & ~(CFGR_PLL_MASK | (0xFu << 4) | (7u << 8) | (7u << 11));
    cfgr |= pll | CFGR_PLLRANGE | (4u << 8) | (4u << 11);   /* AHB /1, APB1 /2, APB2 /2 */
    RCC_CFGR = cfgr;

    RCC_CR |= CR_PLLON;
    if (!wait_set(&RCC_CR, CR_PLLRDY, CR_PLLRDY)) {
        fall_back_to_hsi();
        systick_start();
        return;
    }

    RCC_MISC2 |= 3u << 4;                           /* auto-step on */
    RCC_CFGR = (RCC_CFGR & ~3u) | 2u;               /* SYSCLK = PLL */
    int ok = wait_set(&RCC_CFGR, 0xCu, 0x8u);
    RCC_MISC2 &= ~(3u << 4);
    if (!ok) {
        fall_back_to_hsi();
    } else {
        sysclk_hz = 240000000;
    }
    systick_start();
}

void delay_ms(uint32_t ms)
{
    uint32_t t0 = ms_ticks;
    while (ms_ticks - t0 < ms) IWDG_KR = 0xAAAAu;
}
