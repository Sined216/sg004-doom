#include <string.h>
#include "board.h"
#include "msc.h"
#include "scsi.h"
#include "usb.h"
#include "vfat.h"

#define CBW_SIG 0x43425355u     /* "USBC" */
#define CSW_SIG 0x53425355u     /* "USBS" */

volatile uint32_t msc_reads, msc_cmds, msc_writes_refused;
#ifdef SG_WRITE_BUILD
void (*msc_write_hook)(uint32_t lba, const uint8_t *block);
#endif

static uint8_t sector[VFAT_SECTOR];

/* status owed to the host after a stalled data stage, sent once the host clears the halt */
static struct { uint8_t pending; uint32_t tag, residue; } owed;
static uint8_t resp[64];

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void put_le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static int send_csw(uint32_t tag, uint32_t residue, uint8_t status)
{
    uint8_t c[13];
    put_le32(c, CSW_SIG);
    put_le32(c + 4, tag);
    put_le32(c + 8, residue);
    c[12] = status;
    return usb_in_send(c, 13);
}

void msc_init(void)
{
    scsi_init(vfat_total_sectors());
    msc_reads = msc_cmds = msc_writes_refused = 0;
}

static void drop_pending_out(void)
{
    uint8_t d[64];
    uint32_t n;
    while (usb_out_ready()) usb_out_read(d, &n);
}

/* one command, started by a CBW packet already read into cbw */
static void run_cbw(const uint8_t *cbw, uint32_t n)
{
    if (n != 31 || le32(cbw) != CBW_SIG) {
        usb_in_stall();                                 /* invalid CBW: the host has to reset the transport */
        return;
    }
    uint32_t tag = le32(cbw + 4), dtl = le32(cbw + 8);
    int dir_in = (cbw[12] & 0x80) != 0;
    struct scsi_result r;
    uint8_t cdb[16];
    memcpy(cdb, cbw + 15, 16);
    msc_cmds++;

    scsi_execute(cdb, resp, &r);

    switch (r.kind) {
    case SCSI_NONE:
        if (dtl) {                                      /* host expects data, none to give: stall and report residue */
            if (dir_in) {
                usb_in_stall();
                owed.pending = 1; owed.tag = tag; owed.residue = dtl;
            } else {
                send_csw(tag, dtl, 1);
            }
            return;
        }
        send_csw(tag, 0, r.good ? 0 : 1);
        return;

    case SCSI_DATA_IN: {
        uint32_t len = r.len < dtl ? r.len : dtl;
        if (len && usb_in_send(resp, len) < 0) return;
        send_csw(tag, dtl - len, r.good ? 0 : 1);
        return;
    }

    case SCSI_READ: {
        uint32_t sent = 0;
        for (uint32_t b = 0; b < r.nblocks && sent + VFAT_SECTOR <= dtl; b++) {
            vfat_read(r.lba + b, sector);
            if (usb_in_send(sector, VFAT_SECTOR) < 0) return;
            sent += VFAT_SECTOR;
            msc_reads++;
        }
        send_csw(tag, dtl - sent, 0);
        return;
    }

    case SCSI_WRITE: {
#ifdef SG_WRITE_BUILD
        /* the host's blocks go to the write handler (UF2 blocks are recognised there, everything else is ignored) */
        uint32_t got = 0, c, fill = 0, blk = 0;
        uint8_t d[64];
        while (got < dtl) {
            if (usb_out_wait(d, &c) < 0) return;
            got += c;
            for (uint32_t i = 0; i < c; i++) {
                sector[fill++] = d[i];
                if (fill == VFAT_SECTOR) {
                    if (msc_write_hook) msc_write_hook(r.lba + blk, sector);
                    blk++;
                    fill = 0;
                }
            }
        }
        send_csw(tag, dtl > got ? dtl - got : 0, 0);
        return;
#else
        /* receive and throw away the data stage; nothing is written anywhere */
        uint32_t got = 0, c;
        uint8_t d[64];
        msc_writes_refused++;
        while (got < dtl) {
            if (usb_out_wait(d, &c) < 0) return;
            got += c;
        }
        send_csw(tag, dtl > got ? dtl - got : 0, 1);
        return;
#endif
    }
    }
}

void msc_poll(void)
{
    usb_poll();

    if (usb_take_bot_reset()) {
        owed.pending = 0;
        drop_pending_out();
        scsi_init(vfat_total_sectors());
    }
    if (!usb_configured()) return;

    if (usb_take_halt_cleared() && owed.pending) {
        owed.pending = 0;
        send_csw(owed.tag, owed.residue, 1);
    }

    if (usb_out_ready()) {
        uint8_t cbw[64];
        uint32_t n;
        if (usb_out_read(cbw, &n) == 0) run_cbw(cbw, n);
    }
}
