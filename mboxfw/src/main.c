/*
 * Mbox 1 class-compliant firmware — main entry.
 *
 * Runs on TAS1020A 8051 core, loaded from I²C EEPROM (24C64 at addr 0x50)
 * by the TAS1020A boot ROM. Presents a standard USB Audio Class 1 device
 * (2 ch × 24 bit × 44.1/48 kHz) so no vendor driver is needed.
 */

#include "regs.h"
#include "cs8427.h"
#include "codec.h"
#include "buttons.h"

extern void hw_init(void);
extern void usb_init(void);
extern void usb_service(void);

/*
 * ISR forward declarations. SDCC's vector-table emitter runs in the
 * compilation unit that owns crt0 (main.c here) and only lays down a
 * jump at 0x0003/0x000B/etc when it sees the matching __interrupt(N)
 * declaration in the same TU. The bodies live in isr.c — these
 * declarations exist purely to plant the vector jumps.
 * (Without them: the linker leaves the vector bytes as 0xFF/random, so
 *  the first INT0/Timer0 firing jumps into the middle of some function
 *  and crashes the CPU.)
 */
void isr_int0(void)    __interrupt(0);
void isr_timer0(void)  __interrupt(1);

void main(void)
{
    hw_init();
    cs8427_boot_init();
    codec_init();           /* Rev 20 flow: lcall 0x08cb, lcall 0x0970 */
    usb_init();

    EA = 1;   /* enable interrupts (Timer 0 + INT0) */

    for (;;) {
        usb_service();
        buttons_poll();
    }
}
