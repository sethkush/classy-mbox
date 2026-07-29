// MATCH: image=rev20 addr=0x0006 len=4 func=usb_ev_suspend cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"

/* VECINT 0x16 (SUSR, suspend-request) handler.
 *
 * Entry 0x16 of the VECINT dispatch table at 0x0C93 points here (the table
 * word at 0x0CBF reads 00 06 -- see usb_int0_isr for how the table is
 * indexed), which is why Ghidra records the XREF as CODE:0cbf rather than a
 * call site.
 *
 * The handler itself does nothing but post event 14 to the main loop. All of
 * the actual suspend work -- shutting the ACGs down, blanking the panel,
 * dropping USBCTL.CONN, entering PCON.IDL and then re-initialising on wake --
 * happens outside interrupt context in evt0e_usb_suspend_enter_and_resume
 * (0x0526), because it parks the CPU in idle mode and must not do that with
 * the USB interrupt still being serviced.
 *
 * These four bytes sit in the gap between the INT0 vector (0x0003, 3 bytes)
 * and the timer-0 vector (0x000B); Keil packed a leaf function into the
 * unused vector space. Rev 22 has the identical four bytes at the identical
 * address 0x0006 (75 0a 0e 22), and its table entry 0x16 at 0x0CA9 also
 * reads 00 06. */
void usb_ev_suspend(void) {
    g_event = 14;
}
