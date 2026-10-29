#pragma once
#include <stdint.h>
/* CRC-32 (IEEE 802.3, same as zlib.crc32 and `crc32` tools): feed with crc = 0 first */
uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n);
