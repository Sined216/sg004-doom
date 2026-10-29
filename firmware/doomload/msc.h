/* Bulk-only transport glue: USB endpoints <-> SCSI layer <-> virtual FAT volume. */
#pragma once
#include <stdint.h>

void msc_init(void);
void msc_poll(void);            /* call from the main loop, services at most one command */

extern volatile uint32_t msc_reads;     /* READ(10) blocks served, for the status line */
extern volatile uint32_t msc_cmds;      /* commands seen */
extern volatile uint32_t msc_writes_refused;

#ifdef SG_WRITE_BUILD
/* Write build: called with every 512-byte block the host writes (lba as sent); the handler decides what it is. */
extern void (*msc_write_hook)(uint32_t lba, const uint8_t *block);
#endif
