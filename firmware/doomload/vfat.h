/*
 * Read-only virtual FAT16 volume over an external SPI NOR flash.
 * Files: NOR.BIN (the whole chip, byte for byte) and INFO.TXT (one cluster, text set by the application).
 * One sector per cluster, so NOR byte offset = 512 * (LBA - data start) and nothing is stored in RAM.
 * There is deliberately no write function: this build cannot modify anything.
 */
#pragma once
#include <stdint.h>

#define VFAT_SECTOR 512u

typedef void (*vfat_nor_read_fn)(uint32_t offset, uint8_t *dst, uint32_t n);

/* nor_size: multiple of 512, 512 B .. 16 MB. Returns 0, or -1 if the size cannot be represented. */
int vfat_init(uint32_t nor_size, vfat_nor_read_fn rd);
/* text shown in INFO.TXT, at most 512 bytes, copied */
/* A volume with no NOR.BIN: INFO.TXT and many free clusters (write build: room to copy a UF2 file), 4200..60000 */
int vfat_init_blank(uint32_t data_clusters);
void vfat_set_info(const char *text, uint32_t len);
uint32_t vfat_total_sectors(void);
/* Fills buf with 512 bytes of the volume. Returns 0, or -1 when lba is past the end. */
int vfat_read(uint32_t lba, uint8_t *buf);

/* layout, for tests */
uint32_t vfat_data_start(void);
