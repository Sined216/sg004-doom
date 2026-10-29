/*
 * UF2-style receiver for the NOR loader (write build). The host copies a file; the USB layer hands over every
 * 512-byte block it writes to the drive. Blocks that are not valid UF2 blocks of our family (the FAT, the directory,
 * Windows housekeeping) are ignored; valid ones carry 256 bytes for a NOR address, which is range checked by
 * nor_write.c's guard, so no order and no placement of the host's writes matters.
 *
 * A 4 KB sector is erased on its first block of the session; a page seen twice is compared, not programmed again
 * (programming without an erase would AND the bits).
 */
#pragma once
#include <stdint.h>

struct uf2_stats {
    uint32_t ok;            /* pages programmed and verified */
    uint32_t dup;           /* pages received again with identical data */
    uint32_t mismatch;      /* pages received again with different data */
    uint32_t rejected;      /* valid UF2 magic, but wrong family/size/range/total: not executed */
    uint32_t errors;        /* erase or program failures */
    uint32_t ignored;       /* blocks without UF2 magic */
    uint32_t erased;        /* sectors erased */
    uint32_t total;         /* blocks the file consists of (from the blocks themselves), 0 until the first one */
    int last_err;           /* last nor_wr_* error code */
    uint32_t last_addr;     /* NOR address of the last valid block */
};

extern struct uf2_stats uf2;

void uf2_reset(void);
void uf2_block(uint32_t lba, const uint8_t *block);     /* matches msc_write_hook */
/* 1 when every page of the file has been received and verified and nothing went wrong */
int uf2_complete(void);
