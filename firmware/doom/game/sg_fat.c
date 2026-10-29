/*
 * Replacement for port/data/fat12.c: DOOM.DAT is one logical file cut in two (see sg_layout.h): the first part in the
 * internal flash, the rest behind a small header on the external NOR. fat_mount / fat_open keep the interface img_init
 * uses, so the data layer is unchanged: the file is described by extents whose "cluster numbers" are simply
 * (physical address >> 12) + 2, with the file base 0. Addresses at or above 0x08000000 are read from the internal
 * flash by nor_read (sg_nor.c), the others from the SPI NOR. SAVE.DAT, DOOMX.DAT and DOOMCODE.DMC do not exist here.
 */
#include <string.h>

#include "fat12.h"
#include "nor.h"
#include "sg_layout.h"

/* why the data was refused, for the boot screen: 0 ok, 1 no header on the NOR, 2 the two halves do not belong
   together, 3 unknown header version or sizes */
int sg_data_status;

static uint32_t doom_size, int_len;

int fat_mount(fat_vol_t *v, uint32_t base)
{
    uint8_t h[36];
    if (base != DMC_VOL_MAIN)                       /* no firmware drive */
        return FAT_E_BPB;
    sg_data_status = 1;
    nor_read(SG_NOR_MIN, h, sizeof h);
    if (memcmp(h, SG_MAGIC, 8))
        return FAT_E_BPB;
    sg_data_status = 3;
    uint32_t ver = h[8] | h[9] << 8 | h[10] << 16 | (uint32_t)h[11] << 24;
    doom_size = h[12] | h[13] << 8 | h[14] << 16 | (uint32_t)h[15] << 24;
    int_len = h[16] | h[17] << 8 | h[18] << 16 | (uint32_t)h[19] << 24;
    if (ver != SG_VERSION || doom_size == 0 || doom_size > 0x01000000u || int_len > SG_INT_BYTES || (int_len & 4095u)
        || int_len != (doom_size < SG_INT_BYTES ? (doom_size & ~4095u) : SG_INT_BYTES)
        || doom_size - int_len > SG_NOR_SIZE - SG_NOR_MIN - SG_HDR_BYTES)
        return FAT_E_BPB;
    /* the halves must come from one conversion: the NOR header repeats the image uuid of the internal part */
    sg_data_status = 2;
    if (int_len && memcmp(h + 20, (const void *)(SG_INT_BASE + SG_UUID_OFF_IN_DAT), 16))
        return FAT_E_CHAIN;
    sg_data_status = 0;
    v->fat = v->root = 0;
    v->data = 0;
    v->nroot = 0;
    v->nclus = (uint16_t)((doom_size + 4095u) >> 12);
    v->cshift = 12;
    return FAT_OK;
}

int fat_open(const fat_vol_t *v, const char *name83, dmc_file_t *f)
{
    if (memcmp(name83, "DOOM    DAT", 11))
        return FAT_E_NOFILE;
    f->data = 0;
    f->size = doom_size;
    f->cshift = 12;
    for (uint32_t c = 0; c < v->nclus; c++) {
        uint32_t off = c << 12;
        uint32_t phys = off < int_len ? SG_INT_BASE + off : SG_NOR_MIN + SG_HDR_BYTES + (off - int_len);
        if (ext_add(f, (uint16_t)c, (uint16_t)(2u + (phys >> 12))))
            return FAT_E_FRAG;
    }
    return FAT_OK;
}

int fat_dir(const fat_vol_t *v, const char *name83, uint32_t *addr)
{
    (void)v; (void)name83; (void)addr;
    return FAT_E_NOFILE;
}
