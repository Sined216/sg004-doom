#include "board.h"
#include "nor.h"

#define SPI2_BASE   0x40003800u
#define SPI_CR1     REG(SPI2_BASE + 0x00)
#define SPI_SR      REG(SPI2_BASE + 0x08)
#define SPI_DR      REG16(SPI2_BASE + 0x0C)

#define NOR_CS   12
#define NOR_SCK  13
#define NOR_MISO 14
#define NOR_MOSI 15

#define APB1ENR     REG(RCC_BASE + 0x1C)

static void pin_mode(uint32_t pin, uint32_t mode)
{
    volatile uint32_t *cr = (volatile uint32_t *)(GPIOB_BASE + GPIO_CRH);
    uint32_t sh = (pin - 8) * 4;
    *cr = (*cr & ~(0xFu << sh)) | (mode << sh);
}

static void cs_low(void)  { REG(GPIOB_BASE + GPIO_BRR)  = 1u << NOR_CS; }
static void cs_high(void) { REG(GPIOB_BASE + GPIO_BSRR) = 1u << NOR_CS; }

void nor_init(void)
{
    /* chip select first, so the chip stays deselected while the other pins change */
    cs_high();
    pin_mode(NOR_CS, GM_OUT_PP_2M);

    APB1ENR |= 1u << 14;                            /* SPI2 clock */
    /* mode 3 (CPOL=1, CPHA=1), master, software NSS, fPCLK1/4 = 9 MHz at 72 MHz */
    SPI_CR1 = (1u << 9) | (1u << 8) | (1u << 3) | (1u << 2) | 3u;
    SPI_CR1 |= 1u << 6;                             /* SPE */

    pin_mode(NOR_SCK, GM_AF_PP_50M);
    pin_mode(NOR_MOSI, GM_AF_PP_50M);
    REG(GPIOB_BASE + GPIO_BSRR) = 1u << NOR_MISO;   /* pull-up on MISO */
    pin_mode(NOR_MISO, GM_IN_PULL);
}

volatile uint32_t nor_timeouts;

/* a wedged SPI block must not hang the firmware: give up after ~1 ms worth of polling and count it */
static int spi_wait(uint32_t flag)
{
    for (uint32_t i = 0; i < 20000u; i++)
        if (SPI_SR & flag) return 1;
    nor_timeouts++;
    return 0;
}

static uint8_t xfer(uint8_t out)
{
    if (!spi_wait(2u)) return 0xFF;                 /* TXE */
    SPI_DR = out;
    if (!spi_wait(1u)) return 0xFF;                 /* RXNE */
    return (uint8_t)SPI_DR;
}

int nor_identify(struct nor_id *id)
{
    uint8_t b[3];
    cs_low();
    xfer(0x9F);
    for (int i = 0; i < 3; i++) b[i] = xfer(0xFF);
    cs_high();
    id->mfr = b[0]; id->type = b[1]; id->cap = b[2]; id->size = 0;
    if ((b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x00) || (b[0] == 0xFF && b[1] == 0xFF && b[2] == 0xFF)) return -1;
    if (b[2] < 0x14 || b[2] > 0x18) return -1;
    id->size = 1u << b[2];
    return 0;
}

void nor_read(uint32_t addr, uint8_t *dst, uint32_t n)
{
    cs_low();
    xfer(0x03);
    xfer((uint8_t)(addr >> 16)); xfer((uint8_t)(addr >> 8)); xfer((uint8_t)addr);
    while (n--) *dst++ = xfer(0xFF);
    cs_high();
}

void nor_select(void)   { cs_low(); }
void nor_deselect(void) { cs_high(); }
uint8_t nor_xfer(uint8_t out) { return xfer(out); }
