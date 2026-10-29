/*
 * Clock setup copied from the stock firmware (SetSysClock at 0x08006218): HSE x9 = 72 MHz, flash latency 2
 * with prefetch, APB1 /2. The USB block needs 48 MHz = 72 MHz / 1.5, which is CFGR bit 22 (USBPRE) = 0.
 */
#include "board.h"
#include "clock.h"

uint32_t sysclk_hz = 8000000;
volatile uint32_t ms_ticks;

#define FLASH_ACR   REG(0x40022000u)

#define CR_HSEON    (1u << 16)
#define CR_HSERDY   (1u << 17)
#define CR_PLLON    (1u << 24)
#define CR_PLLRDY   (1u << 25)

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

/* Power-key watchdog: a 2 s hold of POWER switches the device off from here, so that a hang anywhere in the
   main program (SPI, USB) can never leave PB7 latched with no way out. The key must be released once first,
   because it is still held while the device starts. */
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
}

void clock_init(void)
{
    RCC_CR |= 1u;                                   /* HSI on, SYSCLK = HSI, PLL off, known state */
    wait_set(&RCC_CR, 2u, 2u);
    RCC_CFGR &= ~3u;
    wait_set(&RCC_CFGR, 0xCu, 0);
    RCC_CR &= ~CR_PLLON;
    wait_set(&RCC_CR, CR_PLLRDY, 0);

    RCC_CR |= CR_HSEON;
    if (!wait_set(&RCC_CR, CR_HSERDY, CR_HSERDY)) {
        RCC_CR &= ~CR_HSEON;
        systick_start();
        return;                                     /* stay on HSI, the caller refuses to start USB */
    }

    FLASH_ACR = (FLASH_ACR & ~3u) | 0x10u | 2u;     /* prefetch on, latency 2 */
    uint32_t cfgr = RCC_CFGR;
    /* also clear the AT32-only bits the bootloader may have left: PLL range (31), PLL multiplier high bits (30:29),
       USBDIV high bits (27, 23); with USBDIV = 0 and bit 22 = 0 the USB clock is PLL/1.5 = 48 MHz */
    cfgr &= ~((1u << 31) | (3u << 29) | (1u << 27) | (1u << 23) | (1u << 22) | (0xFu << 18) | (1u << 17) |
              (1u << 16) | (7u << 11) | (7u << 8) | (0xFu << 4));
    cfgr |= (1u << 16)          /* PLL source HSE */
          | (7u << 18)          /* x9 */
          | (4u << 8);          /* APB1 /2 */
    RCC_CFGR = cfgr;

    RCC_CR |= CR_PLLON;
    if (!wait_set(&RCC_CR, CR_PLLRDY, CR_PLLRDY)) {
        RCC_CR &= ~CR_PLLON;
        systick_start();
        return;
    }
    RCC_CFGR = (RCC_CFGR & ~3u) | 2u;               /* SYSCLK = PLL */
    if (wait_set(&RCC_CFGR, 0xCu, 0x8u)) sysclk_hz = 72000000;
    else { RCC_CFGR &= ~3u; wait_set(&RCC_CFGR, 0xCu, 0); }
    systick_start();
}

void delay_ms(uint32_t ms)
{
    uint32_t t0 = ms_ticks;
    while (ms_ticks - t0 < ms) IWDG_KR = 0xAAAAu;
}
