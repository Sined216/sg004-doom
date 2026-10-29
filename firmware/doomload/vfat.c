#include <string.h>
#include "vfat.h"

#define RESERVED     1u
#define NFATS        2u
#define ROOT_ENTRIES 512u
#define ROOT_SECTORS (ROOT_ENTRIES * 32u / VFAT_SECTOR)
#define MIN_CLUSTERS 4200u              /* FAT16 needs at least 4085 clusters; pad small chips with free ones */
#define MAX_FILE_CLUSTERS 32768u        /* 16 MB at 512 B per cluster */

static vfat_nor_read_fn nor_rd;
static uint32_t n_file;                 /* clusters (sectors) of NOR.BIN */
static uint32_t n_clusters;             /* data clusters of the volume */
static uint32_t fat_sectors, root_start, data_start, total;
static uint32_t nor_bytes;
static char info_text[VFAT_SECTOR];
static uint32_t info_len;

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

int vfat_init(uint32_t nor_size, vfat_nor_read_fn rd)
{
    if (nor_size == 0 || (nor_size % VFAT_SECTOR) || nor_size / VFAT_SECTOR > MAX_FILE_CLUSTERS) return -1;
    nor_rd = rd;
    nor_bytes = nor_size;
    n_file = nor_size / VFAT_SECTOR;
    n_clusters = n_file + 1;            /* + INFO.TXT */
    if (n_clusters < MIN_CLUSTERS) n_clusters = MIN_CLUSTERS;
    fat_sectors = ((n_clusters + 2) * 2 + VFAT_SECTOR - 1) / VFAT_SECTOR;
    root_start = RESERVED + NFATS * fat_sectors;
    data_start = root_start + ROOT_SECTORS;
    total = data_start + n_clusters;
    info_len = 0;
    return 0;
}

int vfat_init_blank(uint32_t data_clusters)
{
    if (data_clusters < MIN_CLUSTERS || data_clusters > 60000u) return -1;
    nor_rd = 0;
    nor_bytes = 0;
    n_file = 0;
    n_clusters = data_clusters;
    fat_sectors = ((n_clusters + 2) * 2 + VFAT_SECTOR - 1) / VFAT_SECTOR;
    root_start = RESERVED + NFATS * fat_sectors;
    data_start = root_start + ROOT_SECTORS;
    total = data_start + n_clusters;
    info_len = 0;
    return 0;
}

void vfat_set_info(const char *text, uint32_t len)
{
    if (len > sizeof info_text) len = sizeof info_text;
    memcpy(info_text, text, len);
    info_len = len;
}

uint32_t vfat_total_sectors(void) { return total; }
uint32_t vfat_data_start(void)    { return data_start; }

static void boot_sector(uint8_t *b)
{
    memset(b, 0, VFAT_SECTOR);
    b[0] = 0xEB; b[1] = 0x3C; b[2] = 0x90;
    memcpy(b + 3, "MSDOS5.0", 8);
    put16(b + 11, VFAT_SECTOR);
    b[13] = 1;                                  /* sectors per cluster */
    put16(b + 14, RESERVED);
    b[16] = NFATS;
    put16(b + 17, ROOT_ENTRIES);
    put16(b + 19, total < 65536u ? total : 0);
    b[21] = 0xF8;
    put16(b + 22, fat_sectors);
    put16(b + 24, 63);
    put16(b + 26, 255);
    put32(b + 28, 0);                           /* hidden sectors */
    put32(b + 32, total < 65536u ? 0 : total);
    b[36] = 0x80;
    b[38] = 0x29;
    put32(b + 39, 0x4E4F5201u);                 /* volume id */
    memcpy(b + 43, "NORBACKUP  ", 11);
    memcpy(b + 54, "FAT16   ", 8);
    b[510] = 0x55; b[511] = 0xAA;
}

static void fat_sector(uint32_t idx0, uint8_t *b)
{
    for (uint32_t k = 0; k < VFAT_SECTOR / 2; k++) {
        uint32_t i = idx0 + k, v = 0;
        if (i == 0) v = 0xFFF8;
        else if (i == 1) v = 0xFFFF;
        else if (i < 2 + n_file) v = (i == 1 + n_file) ? 0xFFFF : i + 1;
        else if (i == 2 + n_file) v = 0xFFFF;       /* INFO.TXT, one cluster */
        put16(b + 2 * k, v);
    }
}

static void dir_entry(uint8_t *e, const char *name11, uint8_t attr, uint32_t cluster, uint32_t size)
{
    memcpy(e, name11, 11);
    e[11] = attr;
    put16(e + 16, (2026u - 1980u) << 9 | 10u << 5 | 28u);   /* creation date */
    put16(e + 18, (2026u - 1980u) << 9 | 10u << 5 | 28u);   /* access date */
    put16(e + 24, (2026u - 1980u) << 9 | 10u << 5 | 28u);   /* write date */
    put16(e + 26, cluster);
    put32(e + 28, size);
}

int vfat_read(uint32_t lba, uint8_t *b)
{
    if (lba >= total) return -1;
    if (lba == 0) { boot_sector(b); return 0; }
    memset(b, 0, VFAT_SECTOR);
    if (lba < root_start) {
        uint32_t f = lba - RESERVED;
        if (f >= fat_sectors) f -= fat_sectors;
        fat_sector(f * (VFAT_SECTOR / 2), b);
    } else if (lba < data_start) {
        if (lba == root_start) {
            dir_entry(b,      "NORBACKUP  ", 0x08, 0, 0);
            if (nor_bytes) {
                dir_entry(b + 32, "NOR     BIN", 0x21, 2, nor_bytes);        /* read-only | archive */
                dir_entry(b + 64, "INFO    TXT", 0x21, 2 + n_file, info_len);
            } else {
                dir_entry(b + 32, "INFO    TXT", 0x21, 2, info_len);         /* blank volume: just the info file */
            }
        }
    } else {
        uint32_t c = lba - data_start;
        if (c < n_file) nor_rd(c * VFAT_SECTOR, b, VFAT_SECTOR);
        else if (c == n_file) memcpy(b, info_text, info_len);
    }
    return 0;
}
