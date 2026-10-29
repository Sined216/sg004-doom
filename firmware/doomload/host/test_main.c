/*
 * Host test: builds the whole virtual volume over a pseudo-random fake NOR and writes it to an image,
 * and exercises the SCSI layer. host/check_fat.py then parses the image independently.
 *   gcc -O1 -Wall -Wextra -I. -o host/test host/test_main.c vfat.c scsi.c crc32.c
 *   host/test <nor_kib> <image_out> <nor_out>
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../vfat.h"
#include "../scsi.h"
#include "../crc32.h"

static uint8_t *nor;
static uint32_t nor_len;

static void nor_read(uint32_t off, uint8_t *dst, uint32_t n)
{
    assert(off + n <= nor_len);
    memcpy(dst, nor + off, n);
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    nor_len = (uint32_t)atoi(argv[1]) * 1024u;
    nor = malloc(nor_len);
    uint32_t x = 12345;
    for (uint32_t i = 0; i < nor_len; i++) { x = x * 1664525u + 1013904223u; nor[i] = (uint8_t)(x >> 24); }
    FILE *f = fopen(argv[3], "wb"); fwrite(nor, 1, nor_len, f); fclose(f);

    assert(vfat_init(nor_len, nor_read) == 0);
    char info[64];
    uint32_t crc = crc32_update(0, nor, nor_len);
    int n = snprintf(info, sizeof info, "SIZE %u\r\nCRC32 %08X\r\n", nor_len, crc);
    vfat_set_info(info, (uint32_t)n);

    uint32_t total = vfat_total_sectors();
    f = fopen(argv[2], "wb");
    uint8_t b[512];
    for (uint32_t l = 0; l < total; l++) { assert(vfat_read(l, b) == 0); fwrite(b, 1, 512, f); }
    fclose(f);
    assert(vfat_read(total, b) == -1);

    /* SCSI layer */
    uint8_t cdb[16], resp[64];
    struct scsi_result r;
    scsi_init(total);

    memset(cdb, 0, 16); cdb[0] = 0x25;
    scsi_execute(cdb, resp, &r);
    assert(r.good && r.kind == SCSI_DATA_IN && r.len == 8);
    assert(be32(resp) == total - 1);
    assert(be32(resp + 4) == 512);

    memset(cdb, 0, 16); cdb[0] = 0x12; cdb[4] = 36;
    scsi_execute(cdb, resp, &r);
    assert(r.good && r.len == 36 && resp[1] == 0x80);

    memset(cdb, 0, 16); cdb[0] = 0x1A; cdb[4] = 192;
    scsi_execute(cdb, resp, &r);
    assert(r.good && r.len == 4 && (resp[2] & 0x80));           /* write protected */

    memset(cdb, 0, 16); cdb[0] = 0x2A; cdb[8] = 1;
    scsi_execute(cdb, resp, &r);
    assert(!r.good && r.kind == SCSI_WRITE);
    memset(cdb, 0, 16); cdb[0] = 0x03; cdb[4] = 18;
    scsi_execute(cdb, resp, &r);
    assert(r.good && resp[2] == 7 && resp[12] == 0x27);         /* DATA PROTECT, write protected */

    memset(cdb, 0, 16); cdb[0] = 0x28; cdb[8] = 8;
    scsi_execute(cdb, resp, &r);
    assert(r.good && r.kind == SCSI_READ && r.nblocks == 8);
    cdb[2] = (uint8_t)(total >> 24); cdb[3] = (uint8_t)(total >> 16); cdb[4] = (uint8_t)(total >> 8); cdb[5] = (uint8_t)total;
    scsi_execute(cdb, resp, &r);
    assert(!r.good);                                            /* past the end */

    memset(cdb, 0, 16); cdb[0] = 0xA8;
    scsi_execute(cdb, resp, &r);
    assert(!r.good);                                            /* unsupported */

    printf("ok: nor %u KiB, volume %u sectors, crc32 %08X\n", nor_len / 1024, total, crc);
    return 0;
}
