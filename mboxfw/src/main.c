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
#include "eeprom.h"

extern void hw_init(void);
extern void usb_init(void);
extern void usb_service(void);

/*
 * Boot-time DFU escape hatch — mirrors Rev 20's "hold source-1 while
 * plugging in → DFU" behavior. Runs BEFORE hw_init so it works even
 * when USB is completely broken. If P3.3 is held low continuously for
 * ~50 ms after reset, invalidate the EEPROM signature and warm-reset.
 * Next boot lands in boot-ROM bulletproof DFU (0xFFFF:0xFFFE), no
 * hardware disassembly required.
 *
 * This is the SECOND independent software recovery path (the first is
 * mboxflash --enter-dfu → handle_digi_enter_dfu). This one gates on a
 * physical button rather than a USB class request, so it works even
 * when enumeration itself is dead.
 *
 * A glancing touch during plug-in should NOT trigger — the check
 * requires the button to stay low across the full 50 ms sample window.
 */
static void check_boot_dfu_button(void)
{
    unsigned int i;
    unsigned char held = 1;
    for (i = 0; i < 0x5000; i++) {
        if (P3 & P3_BTN_CH1_MASK) { held = 0; break; }
    }
    if (held) {
        if (eeprom_smoke_test()) {
            (void)eeprom_invalidate_signature();
        }
        __asm__("ljmp 0");
    }
}

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

/* Phase-completion canaries — written at each boundary so sim_smoke
 * (and any future runtime verifier that can peek shared RAM) can prove
 * exactly which init phases ran. Different values per phase means we
 * can distinguish "hung in cs8427" from "hung in codec" from "hung in
 * usb_init" without a scope. Canary window is 0xFA00..0xFA03 in the
 * TAS1020A shared-memory area — sits BELOW our EP0 buffers (0xFA10)
 * so it can't collide with USB packet memory. NOVEL — reason: new
 * addresses under 0xFA10 aren't used by TI's engUsbInit or Rev 20's
 * boot init; verified by grepping both. */
#define CANARY_BASE     0xFA00
#define CANARY(n, v)    (*(volatile __xdata unsigned char *)(CANARY_BASE + (n))) = (v)
#define CANARY_MAIN     0xA1   /* main() entered */
#define CANARY_USB      0xA2   /* usb_init returned */
#define CANARY_HW       0xA3   /* hw_init returned */
#define CANARY_CS8427   0xA4   /* cs8427_boot_init returned */
#define CANARY_CODEC    0xA5   /* codec_init returned */
#define CANARY_LOOP     0xA6   /* about to enter main polling loop */

void main(void)
{
    check_boot_dfu_button();

    CANARY(0, CANARY_MAIN);

    /* NEVER-BRICK GUARANTEE (task #47):
     * Bring the USB engine up BEFORE any of the audio-hardware init.
     * usb_init() ends with `USBCTL |= CONN`, which attaches the device
     * to the bus. From this point onward the host can enumerate us and
     * we can respond to the Digi DFU class request in handle_setup —
     * so even if any of hw_init / cs8427_boot_init / codec_init hangs
     * indefinitely, `mboxflash --enter-dfu` still recovers us.
     *
     * Prior ordering (usb_init LAST) meant a single hang in cs8427 or
     * codec bricked the device silently (2026-07-22 flash #2). */
    usb_init();
    CANARY(1, CANARY_USB);

    hw_init();
    CANARY(2, CANARY_HW);

    cs8427_boot_init();
    CANARY(3, CANARY_CS8427);

    codec_init();           /* Rev 20 flow: lcall 0x08cb, lcall 0x0970 */
    /* CANARY_CODEC written into slot 2 to overwrite HW canary — once
     * we've reached here, all four phases before EA=1 are done. Reading
     * XDATA[0xFA02] on a stuck device tells you exactly which init
     * blew up: 0 = pre-hw, 0xA3 = hw done / cs8427 stuck, etc. */
    CANARY(4, CANARY_CODEC);

    EA = 1;   /* enable interrupts (Timer 0 + INT0) */
    CANARY(5, CANARY_LOOP);

    for (;;) {
        usb_service();
        buttons_poll();
    }
}
