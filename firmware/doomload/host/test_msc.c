/*
 * Host test of the bulk-only transport logic (msc.c) with a mock of the USB endpoint layer.
 *   gcc -O1 -Wall -Wextra -I. -o host/test_msc host/test_msc.c msc.c vfat.c scsi.c crc32.c
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../msc.h"
#include "../scsi.h"
#include "../usb.h"
#include "../vfat.h"

/* ---- mock USB layer ---- */
static uint8_t in_buf[1 << 20];
static uint32_t in_len;
static uint8_t out_q[16][64];
static uint32_t out_q_len[16];
static int out_head, out_tail;
static int mock_configured = 1, mock_stalled, mock_halt_cleared, mock_bot_reset;

void usb_poll(void) {}
int usb_configured(void) { return mock_configured; }
int usb_take_bot_reset(void) { int v = mock_bot_reset; mock_bot_reset = 0; return v; }
int usb_take_halt_cleared(void) { int v = mock_halt_cleared; mock_halt_cleared = 0; return v; }
int usb_in_send(const uint8_t *b, uint32_t n)
{
    assert(!mock_stalled && "IN endpoint is halted");
    memcpy(in_buf + in_len, b, n);
    in_len += n;
    return 0;
}
int usb_out_ready(void) { return out_head != out_tail; }
int usb_out_read(uint8_t *b, uint32_t *n)
{
    if (out_head == out_tail) return -1;
    *n = out_q_len[out_head];
    memcpy(b, out_q[out_head], *n);
    out_head = (out_head + 1) % 16;
    return 0;
}
int usb_out_wait(uint8_t *b, uint32_t *n) { return usb_out_read(b, n); }
void usb_in_stall(void) { mock_stalled = 1; }

/* ---- fake NOR ---- */
static uint8_t nor[1024 * 1024];
static void nor_read(uint32_t off, uint8_t *dst, uint32_t n) { memcpy(dst, nor + off, n); }

static void push_out(const uint8_t *p, uint32_t n)
{
    memcpy(out_q[out_tail], p, n);
    out_q_len[out_tail] = n;
    out_tail = (out_tail + 1) % 16;
}

static uint32_t tag_counter;

static void send_cbw(const uint8_t *cdb, uint32_t cdb_len, uint32_t dtl, int dir_in)
{
    uint8_t c[31] = { 'U', 'S', 'B', 'C' };
    tag_counter++;
    memcpy(c + 4, &tag_counter, 4);
    memcpy(c + 8, &dtl, 4);
    c[12] = dir_in ? 0x80 : 0;
    c[14] = (uint8_t)cdb_len;
    memcpy(c + 15, cdb, cdb_len);
    push_out(c, 31);
}

static void check_csw(uint32_t at, uint32_t residue, uint8_t status)
{
    const uint8_t *c = in_buf + at;
    uint32_t tag, res;
    assert(!memcmp(c, "USBS", 4));
    memcpy(&tag, c + 4, 4);
    memcpy(&res, c + 8, 4);
    assert(tag == tag_counter);
    assert(res == residue);
    assert(c[12] == status);
}

int main(void)
{
    uint32_t x = 99;
    for (uint32_t i = 0; i < sizeof nor; i++) { x = x * 1664525u + 1013904223u; nor[i] = (uint8_t)(x >> 24); }
    assert(vfat_init(sizeof nor, nor_read) == 0);
    vfat_set_info("hi", 2);
    msc_init();

    uint8_t cdb[16];

    /* INQUIRY */
    memset(cdb, 0, 16); cdb[0] = 0x12; cdb[4] = 36;
    in_len = 0; send_cbw(cdb, 6, 36, 1); msc_poll();
    assert(in_len == 36 + 13 && in_buf[0] == 0 && in_buf[1] == 0x80);
    check_csw(36, 0, 0);

    /* READ CAPACITY: host asks 8 bytes */
    memset(cdb, 0, 16); cdb[0] = 0x25;
    in_len = 0; send_cbw(cdb, 10, 8, 1); msc_poll();
    assert(in_len == 8 + 13);
    check_csw(8, 0, 0);

    /* READ(10) of 3 sectors from the data area start: must equal the first NOR bytes */
    uint32_t lba = vfat_data_start();
    memset(cdb, 0, 16); cdb[0] = 0x28;
    cdb[2] = (uint8_t)(lba >> 24); cdb[3] = (uint8_t)(lba >> 16); cdb[4] = (uint8_t)(lba >> 8); cdb[5] = (uint8_t)lba;
    cdb[8] = 3;
    in_len = 0; send_cbw(cdb, 10, 3 * 512, 1); msc_poll();
    assert(in_len == 3 * 512 + 13);
    assert(!memcmp(in_buf, nor, 3 * 512));
    check_csw(3 * 512, 0, 0);
    assert(msc_reads == 3);

    /* READ(10) with a host buffer smaller than the transfer: residue reported */
    in_len = 0; send_cbw(cdb, 10, 512, 1); msc_poll();
    assert(in_len == 512 + 13);
    check_csw(512, 0, 0);

    /* WRITE(10): data is received and dropped, status is failed, nothing changes */
    uint8_t before[512];
    nor_read(0, before, 512);
    memset(cdb, 0, 16); cdb[0] = 0x2A; cdb[5] = 0; cdb[8] = 1;
    in_len = 0; send_cbw(cdb, 10, 512, 0);
    uint8_t junk[64]; memset(junk, 0xAB, 64);
    for (int i = 0; i < 8; i++) push_out(junk, 64);
    msc_poll();
    assert(in_len == 13);
    check_csw(0, 0, 1);
    assert(msc_writes_refused == 1);
    assert(out_head == out_tail);                       /* every data packet was consumed */
    uint8_t after[512];
    nor_read(0, after, 512);
    assert(!memcmp(before, after, 512));

    /* unsupported command with no data stage: failed status */
    memset(cdb, 0, 16); cdb[0] = 0xA8;
    in_len = 0; send_cbw(cdb, 12, 0, 1); msc_poll();
    assert(in_len == 13);
    check_csw(0, 0, 1);

    /* unsupported command while the host expects data: IN stalled, status owed until the halt is cleared */
    in_len = 0; send_cbw(cdb, 12, 64, 1); msc_poll();
    assert(mock_stalled && in_len == 0);
    mock_stalled = 0; mock_halt_cleared = 1;
    msc_poll();
    assert(in_len == 13);
    check_csw(0, 64, 1);

    /* bad CBW signature stalls IN */
    uint8_t bad[31] = { 'X' };
    in_len = 0; push_out(bad, 31); msc_poll();
    assert(mock_stalled);
    mock_stalled = 0;

    /* bot reset clears the queue */
    push_out(bad, 31);
    mock_bot_reset = 1; msc_poll();
    assert(out_head == out_tail);

    /* TEST UNIT READY, REQUEST SENSE after the refused write reports nothing pending after being consumed */
    memset(cdb, 0, 16);
    in_len = 0; send_cbw(cdb, 6, 0, 1); msc_poll();
    assert(in_len == 13);
    check_csw(0, 0, 0);

    printf("test_msc ok\n");
    return 0;
}
