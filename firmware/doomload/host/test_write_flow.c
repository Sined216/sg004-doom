/*
 * Host test of the whole write path with the real data file: doom.uf2 made by firmware/doom/game/tools/make_data.py is
 * fed through the bulk-only transport (WRITE(10) commands, chunks in random order, mixed with FAT-like housekeeping
 * writes) into uf2.c / nor_write.c / the W25Q model. Afterwards the model must hold exactly doom.nor.part at
 * SG_NOR_MIN and the History area must be untouched.
 *   gcc -O1 -Wall -Wextra -I. -DSG_WRITE_BUILD -o host/test_write_flow host/test_write_flow.c msc.c vfat.c scsi.c uf2.c nor_write.c crc32.c
 *   host/test_write_flow doom.uf2 doom.nor.part
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fakechip.h"
#include "../crc32.h"
#include "../msc.h"
#include "../nor_write.h"
#include "../scsi.h"
#include "../uf2.h"
#include "../usb.h"
#include "../vfat.h"

volatile uint32_t ms_ticks;
void wdt_feed(void) {}

/* ---- mock USB layer ---- */
static uint8_t in_buf[1 << 16];
static uint32_t in_len;
static uint8_t out_q[4096][64];
static uint32_t out_q_len[4096];
static int out_head, out_tail;

void usb_poll(void) {}
int usb_configured(void) { return 1; }
int usb_take_bot_reset(void) { return 0; }
int usb_take_halt_cleared(void) { return 0; }
int usb_in_send(const uint8_t *b, uint32_t n) { assert(in_len + n <= sizeof in_buf); memcpy(in_buf + in_len, b, n); in_len += n; return 0; }
int usb_out_ready(void) { return out_head != out_tail; }
int usb_out_read(uint8_t *b, uint32_t *n)
{
    if (out_head == out_tail) return -1;
    *n = out_q_len[out_head];
    memcpy(b, out_q[out_head], *n);
    out_head = (out_head + 1) % 4096;
    return 0;
}
int usb_out_wait(uint8_t *b, uint32_t *n) { return usb_out_read(b, n); }
void usb_in_stall(void) {}

static void push_out(const uint8_t *p, uint32_t n)
{
    memcpy(out_q[out_tail], p, n);
    out_q_len[out_tail] = n;
    out_tail = (out_tail + 1) % 4096;
}

static uint32_t tag;

static void write10(uint32_t lba, const uint8_t *data, uint32_t blocks)
{
    uint8_t cbw[31] = {'U', 'S', 'B', 'C'};
    uint32_t dtl = blocks * 512;
    tag++;
    memcpy(cbw + 4, &tag, 4);
    memcpy(cbw + 8, &dtl, 4);
    cbw[14] = 10;
    cbw[15] = 0x2A;
    cbw[17] = (uint8_t)(lba >> 24); cbw[18] = (uint8_t)(lba >> 16); cbw[19] = (uint8_t)(lba >> 8); cbw[20] = (uint8_t)lba;
    cbw[22] = (uint8_t)(blocks >> 8); cbw[23] = (uint8_t)blocks;
    push_out(cbw, 31);
    for (uint32_t o = 0; o < dtl; o += 64) push_out(data + o, 64);
    in_len = 0;
    msc_poll();
    assert(in_len == 13 && !memcmp(in_buf, "USBS", 4) && in_buf[12] == 0);       /* GOOD status */
}

static uint32_t rnd_state = 777;
static uint32_t rnd(void) { rnd_state = rnd_state * 1664525u + 1013904223u; return rnd_state >> 8; }

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long uf2_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *uf2file = malloc((size_t)uf2_size);
    assert(fread(uf2file, 1, (size_t)uf2_size, f) == (size_t)uf2_size);
    fclose(f);
    f = fopen(argv[2], "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long part_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *part = malloc((size_t)part_size);
    assert(fread(part, 1, (size_t)part_size, f) == (size_t)part_size);
    fclose(f);
    uint32_t nblocks = (uint32_t)(uf2_size / 512);

    /* chip: History in the first 31 sectors, everything else blank */
    memset(mem, 0xFF, sizeof mem);
    for (uint32_t i = 0; i < 31u * 4096u; i++) mem[i] = (uint8_t)(i * 13 + (i >> 9));
    uint32_t h0 = crc32_update(0, mem, SG_NOR_MIN);

    /* the write-mode volume: lots of free space, writable */
    assert(vfat_init_blank(16384) == 0);
    vfat_set_info("x", 1);
    scsi_init(vfat_total_sectors());
    uf2_reset();
    msc_write_hook = uf2_block;
    msc_init();
    uint8_t cdb[16], resp[64];
    struct scsi_result r;
    memset(cdb, 0, 16); cdb[0] = 0x25;
    scsi_execute(cdb, resp, &r);
    uint32_t last_lba = (uint32_t)resp[0] << 24 | resp[1] << 16 | resp[2] << 8 | resp[3];
    assert((uint64_t)(last_lba + 1) * 512 > 2ull * (uint64_t)uf2_size);          /* room for the file, twice over */
    memset(cdb, 0, 16); cdb[0] = 0x1A; cdb[4] = 192;
    scsi_execute(cdb, resp, &r);
    assert(!(resp[2] & 0x80));                                                     /* not write protected */

    /* the file in 16-block chunks (8 KB), shuffled, with housekeeping writes in between */
    uint32_t nchunks = (nblocks + 15) / 16;
    uint32_t *order = malloc(nchunks * sizeof *order);
    for (uint32_t i = 0; i < nchunks; i++) order[i] = i;
    for (uint32_t i = nchunks - 1; i > 0; i--) { uint32_t j = rnd() % (i + 1); uint32_t t = order[i]; order[i] = order[j]; order[j] = t; }
    uint8_t noise[512];
    for (uint32_t k = 0; k < nchunks; k++) {
        uint32_t c = order[k], first = c * 16, n = nblocks - first < 16 ? nblocks - first : 16;
        for (int x = 0; x < 512; x++) noise[x] = (uint8_t)rnd();
        write10(rnd() % 4000, noise, 1);                                           /* FAT / directory write */
        write10(100 + first, uf2file + (size_t)first * 512, n);
    }

    assert(uf2_complete());
    assert(uf2.ok == nblocks && uf2.errors == 0 && uf2.mismatch == 0 && uf2.rejected == 0 && uf2.ignored >= nchunks);
    assert(!memcmp(mem + SG_NOR_MIN, part, (size_t)part_size));
    assert(crc32_update(0, mem, SG_NOR_MIN) == h0);
    for (uint32_t a = SG_NOR_MIN + (uint32_t)part_size + 256u; a < SG_NOR_SIZE; a++)
        if (a >= ((SG_NOR_MIN + (uint32_t)part_size + 4095u) & ~4095u)) assert(mem[a] == 0xFF);
    printf("test_write_flow ok: %u UF2 blocks (%ld bytes), NOR part %ld bytes, %u sectors erased, History unchanged, "
           "free space %.1f MB\n", nblocks, uf2_size, part_size, uf2.erased, (last_lba + 1) * 512.0 / 1e6);
    return 0;
}
