/*
 * SCSI transparent command set for a read-only USB mass storage drive (bulk-only transport).
 * Transport independent: the USB layer passes each CDB in and moves the data and status.
 * WRITE(10) is recognised only to consume the data stage and answer DATA PROTECT; nothing is written.
 */
#pragma once
#include <stdint.h>

enum scsi_kind {
    SCSI_NONE,      /* no data stage */
    SCSI_DATA_IN,   /* device-to-host data is in the response buffer, len bytes */
    SCSI_READ,      /* device-to-host: nblocks starting at lba, produced by the volume */
    SCSI_WRITE,     /* host-to-device: nblocks of data to receive and drop; status is already CHECK CONDITION */
};

struct scsi_result {
    enum scsi_kind kind;
    uint8_t  good;      /* 1 = GOOD, 0 = CHECK CONDITION (sense is set) */
    uint32_t len;       /* SCSI_DATA_IN: bytes in the response buffer */
    uint32_t lba, nblocks;
};

/* resp must hold at least 36 bytes */
void scsi_init(uint32_t total_blocks);
void scsi_execute(const uint8_t cdb[16], uint8_t *resp, struct scsi_result *r);
