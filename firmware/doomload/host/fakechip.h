/* Model of the W25Q80 for the host tests (included by the test programs; defines nor_* of the loader). */
#pragma once
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../../doom/game/sg_layout.h"
#include "../nor.h"

extern volatile uint32_t ms_ticks;

/* ---------------- chip model ---------------- */
static uint8_t mem[SG_NOR_SIZE];
static int selected, wel, busy_polls, protect_bits, bad_id, stuck_busy, stuck_addr = -1;
static int cmd, nbytes;
static uint32_t addr;
static uint32_t erase_calls, prog_calls, write_ops_below_min;

static void chip_reset_state(void)
{
    selected = wel = busy_polls = protect_bits = bad_id = stuck_busy = 0;
    stuck_addr = -1;
}

void nor_select(void) { selected = 1; cmd = -1; nbytes = 0; addr = 0; }

void nor_deselect(void)
{
    if (cmd == 0x06) wel = 1;
    if (cmd == 0x20 && nbytes == 3) {
        if (wel && !protect_bits) {
            if (addr < SG_NOR_MIN) write_ops_below_min++;
            memset(mem + (addr & ~4095u), 0xFF, 4096);
            erase_calls++;
            busy_polls = 3;
        }
        wel = 0;
    }
    if (cmd == 0x02) {
        if (nbytes >= 3) {
            prog_calls++;
            busy_polls = 2;
        }
        wel = 0;
    }
    selected = 0;
}

uint8_t nor_xfer(uint8_t b)
{
    assert(selected);
    if (cmd < 0) { cmd = b; nbytes = 0; return 0xFF; }
    int n = nbytes++;
    switch (cmd) {
    case 0x9F: {
        static const uint8_t id[3] = {0xEF, 0x40, 0x14}, bad[3] = {0xC8, 0x40, 0x14};
        return n < 3 ? (bad_id ? bad : id)[n] : 0xFF;
    }
    case 0x05: {
        uint8_t v = (uint8_t)(protect_bits ? 0x04 : 0);
        if (stuck_busy) { ms_ticks++; return v | 1; }
        if (busy_polls > 0) { busy_polls--; ms_ticks++; v |= 1; }
        if (wel) v |= 2;
        return v;
    }
    case 0x35: return 0;
    case 0x20:
        if (n < 3) addr = addr << 8 | b;
        return 0xFF;
    case 0x02:
        if (n < 3) { addr = addr << 8 | b; return 0xFF; }
        if (wel && !protect_bits) {
            uint32_t page = addr & ~255u, off = (addr + (uint32_t)(n - 3)) & 255u;
            uint32_t a = page + off;
            if (a < SG_NOR_MIN) write_ops_below_min++;
            mem[a] &= b;
            if ((int)a == stuck_addr) mem[a] |= 0x01;      /* a stuck bit */
        }
        return 0xFF;
    case 0x03:
        if (n < 3) { addr = addr << 8 | b; return 0xFF; }
        return mem[(addr + (uint32_t)(n - 3)) & (SG_NOR_SIZE - 1)];
    }
    return 0xFF;
}

void nor_read(uint32_t a, uint8_t *dst, uint32_t n) { memcpy(dst, mem + a, n); }

