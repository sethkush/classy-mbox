/*
 * Mbox 1 class-compliant firmware — main entry.
 *
 * Runs on TAS1020A 8051 core, loaded from I²C EEPROM (24C64 at addr 0x50)
 * by the TAS1020A boot ROM. Presents a standard USB Audio Class 1 device
 * (2 ch × 24 bit × 44.1/48 kHz) so no vendor driver is needed.
 */

#include "regs.h"
#include "cs8427.h"
#include "buttons.h"

extern void hw_init(void);
extern void usb_init(void);          /* TODO: descriptors.c */
extern void usb_service(void);       /* TODO: descriptors.c */

void main(void)
{
    hw_init();
    cs8427_boot_init();
    /* TODO: codec_init() once the codec chip is confirmed */
    usb_init();

    EA = 1;   /* enable interrupts (Timer 0 + INT0) */

    for (;;) {
        usb_service();
        buttons_poll();
    }
}
