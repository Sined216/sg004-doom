/*
 * USB full-speed device on the F103-style controller (0x40005C00, packet memory 0x40006000), polled, no interrupts.
 * Bring-up sequence taken from the stock firmware, see STOCK_FIRMWARE_NOTES.md.
 * Endpoints: 0 control, 1 bulk IN (64 B), 2 bulk OUT (64 B): one mass-storage interface.
 */
#pragma once
#include <stdint.h>

void usb_early_detach(void);    /* very early: reset the USB block, power it down and hold D+/D- low (host sees a detach) */
void usb_init(void);            /* releases D+/D-, clock, reset pulse; the host sees the device after this */
void usb_poll(void);            /* services bus reset and endpoint 0; call often */
int  usb_configured(void);
/* set by usb_poll when the host did a bulk-only reset or cleared a halt, cleared by the msc layer */
int  usb_take_bot_reset(void);
int  usb_take_halt_cleared(void);

/* Bulk endpoints. All of them give up (return -1) on a bus reset or after ~1 s. */
int  usb_in_send(const uint8_t *buf, uint32_t n);   /* one transfer, split into 64-byte packets */
int  usb_out_ready(void);                           /* 1 when a packet waits on endpoint 2 */
int  usb_out_read(uint8_t *buf, uint32_t *n);       /* takes the waiting packet (n <= 64) and re-arms the endpoint */
int  usb_out_wait(uint8_t *buf, uint32_t *n);       /* waits for a packet, then as usb_out_read */
void usb_in_stall(void);

/* diagnostics for the status screen */
struct usb_dbg { uint32_t resets, setups; uint8_t last_setup[8]; };
extern struct usb_dbg usb_dbg;
void usb_dbg_regs(uint32_t *daddr, uint32_t *ep0r, uint32_t *istr);
