/*
 * Erase and program of the external W25Q80, WRITE BUILD ONLY (SG004_NORLOAD.bin). This is the only file of the
 * project with write opcodes. Every operation goes through one guard: the address must lie in
 * [SG_NOR_MIN, SG_NOR_SIZE), so the stock History (sectors 0-30) and its headroom (31-55) can never be touched.
 * The status register is only read, never written: if the chip is write protected the loader stops and says so.
 */
#pragma once
#include <stdint.h>

enum {
    NW_OK = 0,
    NW_BAD_CHIP = -1,       /* JEDEC id is not EF 40 14 */
    NW_PROTECTED = -2,      /* BP/TB/SEC (SR1) or CMP (SR2) set: the chip would ignore writes */
    NW_RANGE = -3,          /* address outside the Doom area, or not aligned */
    NW_TIMEOUT = -4,        /* WIP did not clear in time */
    NW_VERIFY = -5,         /* read back differs from what was written */
};

struct nor_wr_status { uint32_t jedec; uint8_t sr1, sr2; };

/* Reads the JEDEC id and both status registers. Returns NW_OK when writing is allowed. */
int nor_wr_probe(struct nor_wr_status *st);
/* 4 KB sector erase, addr aligned to 4 KB */
int nor_wr_erase4k(uint32_t addr);
/* one 256-byte page program (the page must be erased), addr aligned to 256, then a read back */
int nor_wr_page(uint32_t addr, const uint8_t *data);
