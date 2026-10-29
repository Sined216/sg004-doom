#include <string.h>
#include "scsi.h"

#define BLOCK 512u

static uint32_t blocks;
static uint8_t sense_key, sense_asc, sense_ascq;

void scsi_init(uint32_t total_blocks)
{
    blocks = total_blocks;
    sense_key = sense_asc = sense_ascq = 0;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static uint32_t rd_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static void fail(struct scsi_result *r, uint8_t key, uint8_t asc, uint8_t ascq)
{
    sense_key = key; sense_asc = asc; sense_ascq = ascq;
    r->good = 0;
}

void scsi_execute(const uint8_t cdb[16], uint8_t *resp, struct scsi_result *r)
{
    memset(r, 0, sizeof *r);
    r->good = 1;
    r->kind = SCSI_NONE;

    switch (cdb[0]) {
    case 0x00:                                   /* TEST UNIT READY */
    case 0x1E:                                   /* PREVENT/ALLOW MEDIUM REMOVAL */
    case 0x1B:                                   /* START STOP UNIT */
        sense_key = sense_asc = sense_ascq = 0;
        break;

    case 0x03: {                                 /* REQUEST SENSE */
        uint32_t n = cdb[4] < 18 ? cdb[4] : 18;
        memset(resp, 0, 18);
        resp[0] = 0x70; resp[2] = sense_key; resp[7] = 10; resp[12] = sense_asc; resp[13] = sense_ascq;
        sense_key = sense_asc = sense_ascq = 0;
        r->kind = SCSI_DATA_IN; r->len = n;
        break;
    }

    case 0x12: {                                 /* INQUIRY */
        uint32_t n = cdb[4] < 36 ? cdb[4] : 36;
        if ((cdb[1] & 1) || cdb[2]) { fail(r, 5, 0x24, 0); break; }   /* no vital product data pages */
        memset(resp, 0, 36);
        resp[0] = 0x00;                          /* direct access block device */
        resp[1] = 0x80;                          /* removable */
        resp[2] = 0x02; resp[3] = 0x02; resp[4] = 31;
        memcpy(resp + 8,  "SG-004A ", 8);
        #ifdef SG_WRITE_BUILD
        memcpy(resp + 16, "DOOM DATA LOADER", 16);
#else
        memcpy(resp + 16, "NOR BACKUP (RO) ", 16);
#endif
        memcpy(resp + 32, "0.1 ", 4);
        r->kind = SCSI_DATA_IN; r->len = n;
        break;
    }

    case 0x1A: {                                 /* MODE SENSE(6): header only, write protected */
        memset(resp, 0, 4);
        resp[0] = 3;
#ifndef SG_WRITE_BUILD
        resp[2] = 0x80;                          /* WP */
#endif
        r->kind = SCSI_DATA_IN; r->len = cdb[4] < 4 ? cdb[4] : 4;
        break;
    }

    case 0x23:                                   /* READ FORMAT CAPACITIES */
        memset(resp, 0, 12);
        resp[3] = 8;
        be32(resp + 4, blocks);
        resp[8] = 2;                             /* formatted media */
        resp[9] = (BLOCK >> 16) & 0xFF; resp[10] = (BLOCK >> 8) & 0xFF; resp[11] = BLOCK & 0xFF;
        r->kind = SCSI_DATA_IN; r->len = 12;
        break;

    case 0x25:                                   /* READ CAPACITY(10) */
        be32(resp, blocks - 1);
        be32(resp + 4, BLOCK);
        r->kind = SCSI_DATA_IN; r->len = 8;
        break;

    case 0x28: {                                 /* READ(10) */
        uint32_t lba = rd_be32(cdb + 2), n = (uint32_t)cdb[7] << 8 | cdb[8];
        if (lba >= blocks || n > blocks - lba) { fail(r, 5, 0x21, 0); break; }   /* LBA out of range */
        r->kind = SCSI_READ; r->lba = lba; r->nblocks = n;
        break;
    }

    case 0x2A: {                                 /* WRITE(10): refused, data stage still has to be drained */
        r->lba = rd_be32(cdb + 2); r->nblocks = (uint32_t)cdb[7] << 8 | cdb[8];
        r->kind = SCSI_WRITE;
#ifndef SG_WRITE_BUILD
        fail(r, 7, 0x27, 0);                     /* DATA PROTECT, write protected */
#endif
        break;
    }

    default:
        fail(r, 5, 0x20, 0);                     /* ILLEGAL REQUEST, invalid command */
        break;
    }
}
