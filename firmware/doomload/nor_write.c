#include <string.h>
#include "../doom/game/sg_layout.h"
#include "clock.h"
#include "nor.h"
#include "nor_write.h"

/* feeds the watchdog; erase waits can take a few hundred milliseconds */
extern void wdt_feed(void);

#define CMD_WREN   0x06
#define CMD_RDSR1  0x05
#define CMD_RDSR2  0x35
#define CMD_ERASE4 0x20
#define CMD_PROG   0x02
#define CMD_JEDEC  0x9F

#define SR1_WIP    0x01
#define SR1_PROT   0x7C         /* BP0 BP1 BP2 TB SEC */
#define SR2_CMP    0x40

#define ERASE_TIMEOUT_MS 500u   /* datasheet maximum for a 4 KB sector is 400 ms */
#define PAGE_TIMEOUT_MS  20u    /* page program maximum is 3 ms */

/* the one guard: a range inside the Doom area */
static int guard(uint32_t addr, uint32_t len, uint32_t align)
{
    if (addr & (align - 1u)) return NW_RANGE;
    if (addr < SG_NOR_MIN || addr >= SG_NOR_SIZE || len > SG_NOR_SIZE - addr) return NW_RANGE;
    return NW_OK;
}

static uint8_t read_sr(uint8_t cmd)
{
    nor_select();
    nor_xfer(cmd);
    uint8_t v = nor_xfer(0xFF);
    nor_deselect();
    return v;
}

static int wait_idle(uint32_t timeout_ms)
{
    uint32_t t0 = ms_ticks;
    for (;;) {
        wdt_feed();
        if (!(read_sr(CMD_RDSR1) & SR1_WIP)) return NW_OK;
        if (ms_ticks - t0 > timeout_ms) return NW_TIMEOUT;
    }
}

static void write_enable(void)
{
    nor_select();
    nor_xfer(CMD_WREN);
    nor_deselect();
}

static void addr3(uint32_t a)
{
    nor_xfer((uint8_t)(a >> 16));
    nor_xfer((uint8_t)(a >> 8));
    nor_xfer((uint8_t)a);
}

int nor_wr_probe(struct nor_wr_status *st)
{
    nor_select();
    nor_xfer(CMD_JEDEC);
    uint32_t a = nor_xfer(0xFF), b = nor_xfer(0xFF), c = nor_xfer(0xFF);
    nor_deselect();
    st->jedec = a << 16 | b << 8 | c;
    st->sr1 = read_sr(CMD_RDSR1);
    st->sr2 = read_sr(CMD_RDSR2);
    if (st->jedec != 0xEF4014u) return NW_BAD_CHIP;
    if ((st->sr1 & SR1_PROT) || (st->sr2 & SR2_CMP)) return NW_PROTECTED;
    return NW_OK;
}

int nor_wr_erase4k(uint32_t addr)
{
    int r = guard(addr, 4096u, 4096u);
    if (r) return r;
    if ((r = wait_idle(ERASE_TIMEOUT_MS))) return r;
    write_enable();
    nor_select();
    nor_xfer(CMD_ERASE4);
    addr3(addr);
    nor_deselect();
    if ((r = wait_idle(ERASE_TIMEOUT_MS))) return r;
    uint8_t b[64];
    for (uint32_t o = 0; o < 4096u; o += sizeof b) {        /* blank check */
        nor_read(addr + o, b, sizeof b);
        for (unsigned i = 0; i < sizeof b; i++)
            if (b[i] != 0xFF) return NW_VERIFY;
    }
    return NW_OK;
}

int nor_wr_page(uint32_t addr, const uint8_t *data)
{
    int r = guard(addr, 256u, 256u);
    if (r) return r;
    if ((r = wait_idle(PAGE_TIMEOUT_MS))) return r;
    write_enable();
    nor_select();
    nor_xfer(CMD_PROG);
    addr3(addr);
    for (unsigned i = 0; i < 256u; i++) nor_xfer(data[i]);
    nor_deselect();
    if ((r = wait_idle(PAGE_TIMEOUT_MS))) return r;
    uint8_t b[64];
    for (uint32_t o = 0; o < 256u; o += sizeof b) {
        nor_read(addr + o, b, sizeof b);
        if (memcmp(b, data + o, sizeof b)) return NW_VERIFY;
    }
    return NW_OK;
}
