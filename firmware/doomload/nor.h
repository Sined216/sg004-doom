/*
 * External SPI NOR flash, READ ONLY: this file has no program, erase or write-enable code at all.
 * SPI2 on PB12 (CS) PB13 (SCK) PB14 (MISO) PB15 (MOSI), mode 3 (as in the stock firmware).
 * Commands used: 0x9F (JEDEC ID) and 0x03 (read).
 */
#pragma once
#include <stdint.h>

struct nor_id { uint8_t mfr, type, cap; uint32_t size; };

void nor_init(void);
/* Returns 0 and fills id when the chip answers with a plausible JEDEC ID (not 00/FF), size = 1 << cap
   for cap 0x14..0x18 (1..16 MB). Returns -1 otherwise (id still holds the raw bytes). */
int  nor_identify(struct nor_id *id);
void nor_read(uint32_t addr, uint8_t *dst, uint32_t n);

/* Plain SPI primitives for modules that talk to the chip themselves (nor_write.c in the write build). No opcode
   of any kind is defined here. */
void nor_select(void);
void nor_deselect(void);
uint8_t nor_xfer(uint8_t out);
