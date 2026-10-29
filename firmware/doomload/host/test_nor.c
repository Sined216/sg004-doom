/*
 * Host test of nor_write.c and uf2.c against a model of the W25Q80: erase to 0xFF in 4 KB sectors, programming ANDs
 * the bits within a 256-byte page, write enable latch, busy time, protection bits, and injectable faults.
 *   gcc -O1 -Wall -Wextra -I. -o host/test_nor host/test_nor.c nor_write.c uf2.c crc32.c
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../doom/game/sg_layout.h"
#include "../crc32.h"
#include "../nor.h"
#include "../nor_write.h"
#include "../uf2.h"

volatile uint32_t ms_ticks;
void wdt_feed(void) {}

#include "fakechip.h"

/* ---------------- helpers ---------------- */
static uint32_t rnd_state = 12345;
static uint32_t rnd(void) { rnd_state = rnd_state * 1664525u + 1013904223u; return rnd_state >> 8; }

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void make_block(uint8_t *b, uint32_t addr_, const uint8_t *payload, uint32_t n, uint32_t total, uint32_t family)
{
    memset(b, 0, 512);
    put32(b, 0x0A324655u); put32(b + 4, 0x9E5D5157u); put32(b + 8, 0x2000u); put32(b + 12, addr_);
    put32(b + 16, 256); put32(b + 20, n); put32(b + 24, total); put32(b + 28, family);
    memcpy(b + 32, payload, 256);
    put32(b + 508, 0x0AB16F30u);
}

static uint32_t hist_crc(void) { return crc32_update(0, mem, SG_NOR_MIN); }

static void fresh_chip(void)
{
    chip_reset_state();
    memset(mem, 0xFF, sizeof mem);
    for (uint32_t i = 0; i < 31u * 4096u; i++) mem[i] = (uint8_t)(i * 7 + (i >> 8));   /* "History" */
    erase_calls = prog_calls = write_ops_below_min = 0;
    uf2_reset();
}

#define NPAGES 3144u
static uint8_t payload[NPAGES * 256];

static void send_all(int shuffle, int with_noise)
{
    static uint32_t order[NPAGES];
    for (uint32_t i = 0; i < NPAGES; i++) order[i] = i;
    if (shuffle)
        for (uint32_t i = NPAGES - 1; i > 0; i--) { uint32_t j = rnd() % (i + 1); uint32_t t = order[i]; order[i] = order[j]; order[j] = t; }
    uint8_t b[512];
    for (uint32_t k = 0; k < NPAGES; k++) {
        uint32_t i = order[k];
        if (with_noise && k % 97 == 0) {                          /* FAT-like housekeeping blocks */
            for (int x = 0; x < 512; x++) b[x] = (uint8_t)rnd();
            uf2_block(k, b);
        }
        make_block(b, SG_NOR_MIN + 256u * i, payload + 256u * i, i, NPAGES, 0x34304753u);
        uf2_block(k, b);
        if (with_noise && k % 53 == 0) uf2_block(k, b);           /* the host writes some blocks twice */
    }
}

int main(void)
{
    for (uint32_t i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)(rnd() >> 3);

    /* ---- probe */
    struct nor_wr_status st;
    fresh_chip();
    assert(nor_wr_probe(&st) == NW_OK && st.jedec == 0xEF4014u);
    bad_id = 1; assert(nor_wr_probe(&st) == NW_BAD_CHIP); bad_id = 0;
    protect_bits = 1; assert(nor_wr_probe(&st) == NW_PROTECTED); protect_bits = 0;

    /* ---- the guard: nothing below SG_NOR_MIN, nothing at or past the end, no unaligned address */
    fresh_chip();
    uint32_t h0 = hist_crc();
    uint8_t pg[256]; memset(pg, 0, sizeof pg);
    assert(nor_wr_erase4k(0) == NW_RANGE);
    assert(nor_wr_erase4k(30u * 4096u) == NW_RANGE);
    assert(nor_wr_erase4k(SG_NOR_MIN - 4096u) == NW_RANGE);
    assert(nor_wr_erase4k(SG_NOR_MIN + 256u) == NW_RANGE);
    assert(nor_wr_erase4k(SG_NOR_SIZE) == NW_RANGE);
    assert(nor_wr_erase4k(0xFFFFF000u) == NW_RANGE);
    assert(nor_wr_page(0, pg) == NW_RANGE);
    assert(nor_wr_page(SG_NOR_MIN - 256u, pg) == NW_RANGE);
    assert(nor_wr_page(SG_NOR_MIN + 1u, pg) == NW_RANGE);
    assert(nor_wr_page(SG_NOR_SIZE, pg) == NW_RANGE);
    assert(erase_calls == 0 && prog_calls == 0 && hist_crc() == h0);

    /* ---- a whole session, out of order, with noise and duplicates */
    send_all(1, 1);
    assert(uf2_complete());
    assert(uf2.ok == NPAGES && uf2.errors == 0 && uf2.mismatch == 0 && uf2.rejected == 0);
    assert(uf2.dup > 0 && uf2.ignored > 0);
    assert(!memcmp(mem + SG_NOR_MIN, payload, sizeof payload));
    assert(hist_crc() == h0 && write_ops_below_min == 0);
    uint32_t end = SG_NOR_MIN + (uint32_t)sizeof payload;
    for (uint32_t a = (end + 4095u) & ~4095u; a < SG_NOR_SIZE; a++) assert(mem[a] == 0xFF);   /* untouched behind the data */

    /* ---- a valid-looking block with a bad address, family or size is rejected and executed never */
    fresh_chip();
    uint8_t b[512];
    make_block(b, 0x1000, payload, 0, 10, 0x34304753u); uf2_block(0, b);                 /* History address */
    make_block(b, SG_NOR_MIN - 256u, payload, 0, 10, 0x34304753u); uf2_block(0, b);
    make_block(b, SG_NOR_SIZE, payload, 0, 10, 0x34304753u); uf2_block(0, b);
    make_block(b, SG_NOR_MIN + 1u, payload, 0, 10, 0x34304753u); uf2_block(0, b);
    make_block(b, SG_NOR_MIN, payload, 0, 10, 0x12345678u); uf2_block(0, b);            /* other family */
    make_block(b, SG_NOR_MIN, payload, 0, 10, 0x34304753u); put32(b + 16, 255); uf2_block(0, b);   /* size */
    assert(uf2.rejected == 6 && uf2.ok == 0 && erase_calls == 0 && prog_calls == 0 && hist_crc() == h0);

    /* ---- a page received again with other data is flagged, the stored one is not overwritten */
    fresh_chip();
    make_block(b, SG_NOR_MIN, payload, 0, 10, 0x34304753u); uf2_block(0, b);
    assert(uf2.ok == 1);
    uint8_t other[256]; memset(other, 0x5A, sizeof other);
    make_block(b, SG_NOR_MIN, other, 0, 10, 0x34304753u); uf2_block(0, b);
    assert(uf2.mismatch == 1 && !memcmp(mem + SG_NOR_MIN, payload, 256));
    assert(!uf2_complete());

    /* ---- a session cut short, then the whole file again: the half-written sectors are erased and redone */
    fresh_chip();
    for (uint32_t i = 0; i < NPAGES / 2; i++) { make_block(b, SG_NOR_MIN + 256u * i, payload + 256u * i, i, NPAGES, 0x34304753u); uf2_block(0, b); }
    assert(!uf2_complete());
    uf2_reset();                                                  /* power cycle: the bitmaps are gone */
    send_all(1, 0);
    assert(uf2_complete() && !memcmp(mem + SG_NOR_MIN, payload, sizeof payload) && hist_crc() == h0);

    /* ---- failures are counted, never reported as done */
    fresh_chip();
    stuck_busy = 1;
    make_block(b, SG_NOR_MIN, payload, 0, 10, 0x34304753u); uf2_block(0, b);
    assert(uf2.errors == 1 && uf2.last_err == NW_TIMEOUT && !uf2_complete());
    stuck_busy = 0;
    fresh_chip();
    stuck_addr = (int)(SG_NOR_MIN + 100u);
    memset(pg, 0x00, sizeof pg);
    make_block(b, SG_NOR_MIN, pg, 0, 10, 0x34304753u); uf2_block(0, b);
    assert(uf2.errors == 1 && uf2.last_err == NW_VERIFY && !uf2_complete());
    fresh_chip();
    protect_bits = 1;                                             /* the chip silently ignores writes */
    make_block(b, SG_NOR_MIN, payload, 0, 10, 0x34304753u); uf2_block(0, b);
    assert(uf2.errors == 1 && !uf2_complete() && hist_crc() == h0);

    printf("test_nor ok: %u pages, History CRC %08X unchanged, writes below SG_NOR_MIN: %u\n", NPAGES, h0, write_ops_below_min);
    return 0;
}
