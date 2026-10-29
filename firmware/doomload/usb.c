#include <string.h>
#include "board.h"
#include "clock.h"
#include "usb.h"

#define USB_BASE    0x40005C00u
#define USB_EPR(n)  REG(USB_BASE + 4u * (n))
#define USB_CNTR    REG(USB_BASE + 0x40)
#define USB_ISTR    REG(USB_BASE + 0x44)
#define USB_DADDR   REG(USB_BASE + 0x4C)
#define USB_BTABLE  REG(USB_BASE + 0x50)
#define PMA_BASE    0x40006000u

#define APB1ENR     REG(RCC_BASE + 0x1C)

/* ISTR */
#define ISTR_CTR    (1u << 15)
#define ISTR_RESET  (1u << 10)
#define ISTR_SUSP   (1u << 11)
#define ISTR_WKUP   (1u << 12)
#define ISTR_DIR    (1u << 4)

/* EPnR */
#define EPR_CTR_RX  (1u << 15)
#define EPR_DTOG_RX (1u << 14)
#define EPR_SETUP   (1u << 11)
#define EPR_CTR_TX  (1u << 7)
#define EPR_DTOG_TX (1u << 6)
#define EPR_KEEP    0x070Fu                         /* EA, KIND, TYPE: written back unchanged */
#define EP_BULK     0x0000u
#define EP_CONTROL  0x0200u

#define ST_DISABLED 0u
#define ST_STALL    1u
#define ST_NAK      2u
#define ST_VALID    3u

/* packet memory map (bytes): buffer descriptor table, then the four buffers */
#define PM_BTABLE   0u
#define PM_EP0_RX   64u
#define PM_EP0_TX   128u
#define PM_EP1_TX   192u
#define PM_EP2_RX   256u

static volatile uint16_t *pma(uint32_t off) { return (volatile uint16_t *)(PMA_BASE + off * 2u); }

static void pma_write(uint32_t off, const uint8_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i += 2)
        *pma(off + i) = (uint16_t)(src[i] | ((i + 1 < n ? src[i + 1] : 0u) << 8));
}

static void pma_read(uint32_t off, uint8_t *dst, uint32_t n)
{
    for (uint32_t i = 0; i < n; i += 2) {
        uint16_t w = *pma(off + i);
        dst[i] = (uint8_t)w;
        if (i + 1 < n) dst[i + 1] = (uint8_t)(w >> 8);
    }
}

/* descriptor table fields of endpoint n: ADDR_TX +0, COUNT_TX +2, ADDR_RX +4, COUNT_RX +6 */
static void set_tx_count(uint32_t n, uint32_t c) { *pma(PM_BTABLE + n * 8 + 2) = (uint16_t)c; }
static uint32_t get_rx_count(uint32_t n)         { return *pma(PM_BTABLE + n * 8 + 6) & 0x3FFu; }

/* ---- endpoint register helpers: toggle bits are written as "wanted XOR current", CTR bits as 1 = no change ---- */

static void ep_stat_tx(uint32_t n, uint32_t s)
{
    uint32_t r = USB_EPR(n);
    USB_EPR(n) = (r & EPR_KEEP) | EPR_CTR_RX | EPR_CTR_TX | ((r & 0x30u) ^ (s << 4));
}

static void ep_stat_rx(uint32_t n, uint32_t s)
{
    uint32_t r = USB_EPR(n);
    USB_EPR(n) = (r & EPR_KEEP) | EPR_CTR_RX | EPR_CTR_TX | ((r & 0x3000u) ^ (s << 12));
}

static void ep_clear_ctr_rx(uint32_t n) { USB_EPR(n) = (USB_EPR(n) & EPR_KEEP) | EPR_CTR_TX; }
static void ep_clear_ctr_tx(uint32_t n) { USB_EPR(n) = (USB_EPR(n) & EPR_KEEP) | EPR_CTR_RX; }

static void ep_dtog_tx_clear(uint32_t n)
{
    uint32_t r = USB_EPR(n);
    USB_EPR(n) = (r & EPR_KEEP) | EPR_CTR_RX | EPR_CTR_TX | (r & EPR_DTOG_TX);
}

static void ep_dtog_rx_clear(uint32_t n)
{
    uint32_t r = USB_EPR(n);
    USB_EPR(n) = (r & EPR_KEEP) | EPR_CTR_RX | EPR_CTR_TX | (r & EPR_DTOG_RX);
}

/* rx buffer size field: 64 bytes = block size 32 (bit 15) x 2 blocks (num_block = 1) */
#define RX_COUNT_64 (0x8000u | (1u << 10))

/* ---- device state ---- */

static uint8_t configured;
static uint8_t pending_addr;            /* 0 = none */
static uint8_t bot_reset_flag, halt_cleared_flag, in_halted;

static const uint8_t *ctl_data;         /* IN data stage */
static uint16_t ctl_left;
static uint8_t ctl_need_zlp;
static uint8_t ctl_stage;               /* 0 idle, 1 IN data, 2 status IN (zero length), 3 status OUT */

static const uint8_t dev_desc[18] = {
    18, 1, 0x00, 0x02, 0, 0, 0, 64,
    0x09, 0x12, 0x01, 0x00,             /* VID 0x1209 (pid.codes), PID 0x0001: generic test device */
    0x00, 0x01, 1, 2, 3, 1,
};

static const uint8_t cfg_desc[32] = {
    9, 2, 32, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 2, 0x08, 0x06, 0x50, 0, /* mass storage, SCSI transparent, bulk-only */
    7, 5, 0x81, 0x02, 64, 0, 0,
    7, 5, 0x02, 0x02, 64, 0, 0,
};

static const uint8_t str0[4] = { 4, 3, 0x09, 0x04 };

static uint8_t str_buf[48];

static const uint8_t *string_desc(uint32_t idx, uint32_t *len)
{
    static const char *const s[] = { "SG-004A", "NOR backup (read-only)", "0001" };
    if (idx == 0) { *len = 4; return str0; }
    if (idx > 3) return 0;
    const char *t = s[idx - 1];
    uint32_t n = 0;
    while (t[n] && n < 22) { str_buf[2 + 2 * n] = (uint8_t)t[n]; str_buf[3 + 2 * n] = 0; n++; }
    str_buf[0] = (uint8_t)(2 + 2 * n); str_buf[1] = 3;
    *len = str_buf[0];
    return str_buf;
}

static void ctl_send_chunk(void)
{
    uint32_t n = ctl_left > 64 ? 64 : ctl_left;
    pma_write(PM_EP0_TX, ctl_data, n);
    set_tx_count(0, n);
    ctl_data += n;
    ctl_left -= (uint16_t)n;
    if (n < 64) ctl_need_zlp = 0;
    ep_stat_tx(0, ST_VALID);
}

static void ctl_status_in(void)
{
    ctl_stage = 2;
    set_tx_count(0, 0);
    ep_stat_tx(0, ST_VALID);
    ep_stat_rx(0, ST_VALID);
}

static void ctl_stall(void)
{
    ctl_stage = 0;
    ep_stat_tx(0, ST_STALL);
    ep_stat_rx(0, ST_STALL);
}

static void ctl_reply(const uint8_t *d, uint32_t len, uint32_t want)
{
    if (len > want) len = want;
    ctl_data = d;
    ctl_left = (uint16_t)len;
    ctl_need_zlp = (len < want) && (len % 64u == 0) && len != 0;
    ctl_stage = 1;
    ctl_send_chunk();
    ep_stat_rx(0, ST_VALID);
}

static void configure_endpoints(void)
{
    /* EP1: bulk IN, EP2: bulk OUT */
    USB_EPR(1) = EP_BULK | 1u;
    ep_dtog_tx_clear(1);
    ep_stat_tx(1, ST_NAK);
    USB_EPR(2) = EP_BULK | 2u;
    ep_dtog_rx_clear(2);
    ep_stat_rx(2, ST_VALID);
    in_halted = 0;
}

static void handle_setup(const uint8_t *s)
{
    uint8_t type = s[0], req = s[1];
    uint16_t value = (uint16_t)(s[2] | (s[3] << 8)), index = (uint16_t)(s[4] | (s[5] << 8)), len = (uint16_t)(s[6] | (s[7] << 8));
    static uint8_t one;

    if ((type & 0x60) == 0) {                                   /* standard requests */
        switch (req) {
        case 6: {                                               /* GET_DESCRIPTOR */
            uint32_t dl;
            const uint8_t *d;
            switch (value >> 8) {
            case 1: ctl_reply(dev_desc, sizeof dev_desc, len); return;
            case 2: ctl_reply(cfg_desc, sizeof cfg_desc, len); return;
            case 3:
                d = string_desc(value & 0xFF, &dl);
                if (d) { ctl_reply(d, dl, len); return; }
                break;
            default: break;
            }
            ctl_stall();
            return;
        }
        case 5:                                                 /* SET_ADDRESS, applied after the status stage */
            pending_addr = (uint8_t)(value & 0x7F);
            ctl_status_in();
            return;
        case 9:                                                 /* SET_CONFIGURATION */
            configured = (uint8_t)(value & 0xFF);
            if (configured) configure_endpoints();
            ctl_status_in();
            return;
        case 8:                                                 /* GET_CONFIGURATION */
            one = configured;
            ctl_reply(&one, 1, len);
            return;
        case 0: {                                               /* GET_STATUS */
            static uint8_t st[2];
            st[0] = ((type & 0x1F) == 2 && index == 0x81) ? in_halted : 0;
            st[1] = 0;
            ctl_reply(st, 2, len);
            return;
        }
        case 1:                                                 /* CLEAR_FEATURE */
            if ((type & 0x1F) == 2 && value == 0) {             /* ENDPOINT_HALT */
                if (index == 0x81) {
                    ep_dtog_tx_clear(1);
                    ep_stat_tx(1, ST_NAK);
                    in_halted = 0;
                    halt_cleared_flag = 1;
                } else if (index == 0x02) {
                    ep_dtog_rx_clear(2);
                    ep_stat_rx(2, ST_VALID);
                }
            }
            ctl_status_in();
            return;
        case 11:                                                /* SET_INTERFACE */
            configure_endpoints();
            ctl_status_in();
            return;
        case 10:                                                /* GET_INTERFACE */
            one = 0;
            ctl_reply(&one, 1, len);
            return;
        default:
            ctl_stall();
            return;
        }
    }
    if ((type & 0x60) == 0x20) {                                /* class requests of the bulk-only transport */
        if (req == 0xFE && (type & 0x80)) {                     /* GET_MAX_LUN */
            one = 0;
            ctl_reply(&one, 1, len);
            return;
        }
        if (req == 0xFF && !(type & 0x80)) {                    /* BULK-ONLY MASS STORAGE RESET */
            bot_reset_flag = 1;
            configure_endpoints();
            ctl_status_in();
            return;
        }
    }
    ctl_stall();
}

static void ep0_service(void)
{
    uint32_t r = USB_EPR(0);
    if (r & EPR_CTR_RX) {
        if (r & EPR_SETUP) {
            uint8_t s[8];
            pma_read(PM_EP0_RX, s, 8);
            memcpy(usb_dbg.last_setup, s, 8);
            usb_dbg.setups++;
            ep_clear_ctr_rx(0);
            ctl_stage = 0;
            handle_setup(s);
        } else {
            ep_clear_ctr_rx(0);                                 /* status OUT of an IN transfer, or stray data */
            ctl_stage = 0;
            ep_stat_rx(0, ST_VALID);
        }
    }
    if (r & EPR_CTR_TX) {
        ep_clear_ctr_tx(0);
        if (pending_addr) {
            USB_DADDR = 0x80u | pending_addr;
            pending_addr = 0;
        }
        if (ctl_stage == 1) {
            if (ctl_left || ctl_need_zlp) {
                if (!ctl_left) ctl_need_zlp = 0;
                ctl_send_chunk();
            } else {
                ctl_stage = 3;                                  /* waiting for the status OUT, RX is already valid */
            }
        } else if (ctl_stage == 2) {
            ctl_stage = 0;
        }
    }
}

static void bus_reset(void)
{
    USB_BTABLE = PM_BTABLE;
    /* descriptor table */
    *pma(PM_BTABLE + 0 * 8 + 0) = PM_EP0_TX;
    *pma(PM_BTABLE + 0 * 8 + 2) = 0;
    *pma(PM_BTABLE + 0 * 8 + 4) = PM_EP0_RX;
    *pma(PM_BTABLE + 0 * 8 + 6) = RX_COUNT_64;
    *pma(PM_BTABLE + 1 * 8 + 0) = PM_EP1_TX;
    *pma(PM_BTABLE + 1 * 8 + 2) = 0;
    *pma(PM_BTABLE + 2 * 8 + 4) = PM_EP2_RX;
    *pma(PM_BTABLE + 2 * 8 + 6) = RX_COUNT_64;

    configured = 0;
    pending_addr = 0;
    ctl_stage = 0;
    in_halted = 0;
    USB_EPR(0) = EP_CONTROL;
    ep_dtog_tx_clear(0);
    ep_dtog_rx_clear(0);
    ep_stat_tx(0, ST_NAK);
    ep_stat_rx(0, ST_VALID);
    USB_DADDR = 0x80u;                                          /* enable, address 0 */
}

struct usb_dbg usb_dbg;

static void usb_pin_mode(uint32_t pin, uint32_t mode)
{
    volatile uint32_t *cr = (volatile uint32_t *)(GPIOA_BASE + GPIO_CRH);
    uint32_t sh = (pin - 8) * 4;
    *cr = (*cr & ~(0xFu << sh)) | (mode << sh);
}

#define APB1RSTR    REG(RCC_BASE + 0x10)

void usb_early_detach(void)
{
    /* Mirror of the stock "power off" routine, but first force the block back to its reset state: the FNIRSI
       bootloader was an enumerated USB device a moment ago and may have left it powered and owning the pins.
       The D+ pull-up on the board is fixed, so the host only sees a detach while D+ is held low. */
    APB1ENR |= 1u << 23;                                        /* clock, needed to touch the block */
    APB1RSTR |= 1u << 23;
    APB1RSTR &= ~(1u << 23);                                    /* CNTR back to PDWN | FRES */
    USB_CNTR = 3u;
    REG(GPIOA_BASE + GPIO_BRR) = (1u << 11) | (1u << 12);
    usb_pin_mode(11, GM_OUT_PP_2M);
    usb_pin_mode(12, GM_OUT_PP_2M);
}

void usb_init(void)
{
    /* release the pins (floating inputs; the USB block takes them over once it is powered) after a final 20 ms
       of detach, then bring-up in the stock order */
    delay_ms(20);
    usb_pin_mode(11, 0x4u);
    usb_pin_mode(12, 0x4u);

    /* Same order as the stock library: USB clock source PLL/1.5 (done in clock_init), clock on, leave power-down,
       reset pulse, clear flags, unmask reset/suspend/wake-up */
    APB1ENR |= 1u << 23;
    USB_CNTR = 1u;                                              /* FRES, PDWN cleared: powered, held in reset */
    delay_ms(2);
    USB_CNTR = 0;
    USB_ISTR = 0;
    USB_CNTR = 0x1C00u;
    (void)USB_CNTR;
}

void usb_poll(void)
{
    uint32_t istr = USB_ISTR;
    if (istr & ISTR_RESET) {
        USB_ISTR = ~ISTR_RESET;
        usb_dbg.resets++;
        bus_reset();
        bot_reset_flag = 1;
        return;
    }
    if (istr & ISTR_SUSP) USB_ISTR = ~ISTR_SUSP;                /* never actually suspend: no FSUSP, just ignore */
    if (istr & ISTR_WKUP) USB_ISTR = ~ISTR_WKUP;
    for (int guard = 0; guard < 16 && ((istr = USB_ISTR) & ISTR_CTR); guard++) {
        if ((istr & 0xF) == 0) ep0_service();
        else break;                                             /* bulk events are taken by the msc layer */
    }
}

int usb_configured(void)       { return configured; }
int usb_take_bot_reset(void)   { int v = bot_reset_flag; bot_reset_flag = 0; return v; }
int usb_take_halt_cleared(void){ int v = halt_cleared_flag; halt_cleared_flag = 0; return v; }

/* waits until (EPnR & flag) while keeping endpoint 0 alive; -1 on bus reset or timeout */
static int wait_flag(uint32_t n, uint32_t flag)
{
    uint32_t t0 = ms_ticks;
    while (!(USB_EPR(n) & flag)) {
        IWDG_KR = 0xAAAAu;
        if (USB_ISTR & ISTR_RESET) return -1;
        if (USB_ISTR & ISTR_CTR) {
            if ((USB_ISTR & 0xF) == 0) ep0_service();
        }
        if (ms_ticks - t0 > 1000u) return -1;
    }
    return 0;
}

int usb_in_send(const uint8_t *buf, uint32_t n)
{
    do {
        uint32_t c = n > 64 ? 64 : n;
        pma_write(PM_EP1_TX, buf, c);
        set_tx_count(1, c);
        ep_stat_tx(1, ST_VALID);
        if (wait_flag(1, EPR_CTR_TX) < 0) return -1;
        ep_clear_ctr_tx(1);
        buf += c;
        n -= c;
    } while (n);
    return 0;
}

int usb_out_ready(void) { return (USB_EPR(2) & EPR_CTR_RX) != 0; }

int usb_out_read(uint8_t *buf, uint32_t *n)
{
    if (!usb_out_ready()) return -1;
    uint32_t c = get_rx_count(2);
    if (c > 64) c = 64;
    pma_read(PM_EP2_RX, buf, c);
    ep_clear_ctr_rx(2);
    ep_stat_rx(2, ST_VALID);
    *n = c;
    return 0;
}

int usb_out_wait(uint8_t *buf, uint32_t *n)
{
    if (wait_flag(2, EPR_CTR_RX) < 0) return -1;
    return usb_out_read(buf, n);
}

void usb_in_stall(void)
{
    in_halted = 1;
    ep_stat_tx(1, ST_STALL);
}

void usb_dbg_regs(uint32_t *daddr, uint32_t *ep0r, uint32_t *istr)
{
    *daddr = USB_DADDR & 0xFFu;
    *ep0r = USB_EPR(0) & 0xFFFFu;
    *istr = USB_ISTR & 0xFFFFu;
}
