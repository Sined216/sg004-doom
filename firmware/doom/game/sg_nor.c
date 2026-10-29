/*
 * External SPI NOR of the SG-004A for the DMC100 engine (interface of dmc100/platform/nor.h), READ ONLY: erase and
 * program return -1 and no write-enable or program command exists in this file. SPI2: CS PB12, SCK PB13, MISO PB14,
 * MOSI PB15, mode 3, hardware SPI, plain 0x03 reads. All "asynchronous" reads complete before they return.
 */
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

/* SPI2 sits on APB1 (120 MHz at 240 MHz core): BR = 2 gives /8 = 15 MHz, well inside the chip's 50 MHz */
#ifndef NOR_SPI_BR
#define NOR_SPI_BR 2u
#endif

volatile struct nor_stats nor_stats;

static uint32_t jedec_id;

static void pin_mode(uint32_t pin, uint32_t mode)
{
    volatile uint32_t *cr = (volatile uint32_t *)(GPIOB_BASE + GPIO_CRH);
    uint32_t sh = (pin - 8) * 4;
    *cr = (*cr & ~(0xFu << sh)) | (mode << sh);
}

static void cs_low(void)  { REG(GPIOB_BASE + GPIO_BRR)  = 1u << NOR_CS; }
static void cs_high(void) { REG(GPIOB_BASE + GPIO_BSRR) = 1u << NOR_CS; }

static int spi_wait(uint32_t flag)
{
    for (uint32_t i = 0; i < 200000u; i++)
        if (SPI_SR & flag) return 1;
    nor_stats.errors++;
    return 0;
}

static uint8_t xfer(uint8_t out)
{
    if (!spi_wait(2u)) return 0xFF;                 /* TXE */
    SPI_DR = out;
    if (!spi_wait(1u)) return 0xFF;                 /* RXNE */
    return (uint8_t)SPI_DR;
}

int nor_init(void)
{
    cs_high();
    pin_mode(NOR_CS, GM_OUT_PP_2M);
    REG(RCC_BASE + 0x1C) |= 1u << 14;               /* SPI2 clock */
    SPI_CR1 = (1u << 9) | (1u << 8) | (NOR_SPI_BR << 3) | (1u << 2) | 3u;    /* mode 3, master, software NSS */
    SPI_CR1 |= 1u << 6;
    pin_mode(NOR_SCK, GM_AF_PP_50M);
    pin_mode(NOR_MOSI, GM_AF_PP_50M);
    REG(GPIOB_BASE + GPIO_BSRR) = 1u << NOR_MISO;
    pin_mode(NOR_MISO, GM_IN_PULL);

    cs_low();
    xfer(0x9F);
    uint32_t a = xfer(0xFF), b = xfer(0xFF), c = xfer(0xFF);
    cs_high();
    jedec_id = a << 16 | b << 8 | c;
    return (jedec_id == 0 || jedec_id == 0xFFFFFFu) ? -1 : 0;
}

uint32_t nor_jedec(void) { return jedec_id; }
uint8_t nor_status(uint8_t cmd) { (void)cmd; return 0; }

void nor_read(uint32_t phys, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    if (phys >= 0x08000000u) {                      /* internal flash: the front part of the data, read in place */
        const uint8_t *src = (const uint8_t *)phys;
        while (n--) *d++ = *src++;
        return;
    }
    nor_stats.sync++;
    nor_stats.bytes += n;
    cs_low();
    xfer(0x03);
    xfer((uint8_t)(phys >> 16)); xfer((uint8_t)(phys >> 8)); xfer((uint8_t)phys);
    while (n--) *d++ = xfer(0xFF);
    cs_high();
}

int nor_read_async(uint32_t phys, void *dst, uint32_t n, int prio)
{
    (void)prio;
    nor_stats.async++;
    nor_read(phys, dst, n);
    return 1;
}

bool nor_done(int ticket) { (void)ticket; return true; }
void nor_wait(int ticket) { (void)ticket; }
void nor_wait_all(void) {}

/* -1: "the bus is not free for a column read, queue it": the caller then uses nor_read_async */
int nor_col_start(uint32_t phys, void *dst, uint32_t n) { (void)phys; (void)dst; (void)n; return -1; }
void nor_col_wait(void) {}

int nor_erase(uint32_t phys, unsigned cmd) { (void)phys; (void)cmd; return -1; }
int nor_program(uint32_t phys, const void *src, uint32_t n) { (void)phys; (void)src; (void)n; return -1; }
void nor_recover(void) {}
int nor_read16(uint32_t phys, void *dst, uint32_t n) { (void)phys; (void)dst; (void)n; return -1; }
