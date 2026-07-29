// MATCH: image=rev22 addr=0x0006 len=4 func=usb_susr_handler cflags=--peep-file,firmware_stock/decomp/keil.peep

/* VECINT 0x16 (SUSR_INT, USB suspend-request) handler, Rev 22.
 *
 * Entry 22 of the VECINT dispatch table at 0x0C7D points here: the table word
 * at 0x0CA9 reads 00 06. That is why Ghidra records the XREF as CODE:0ca9
 * rather than a call site -- nothing LCALLs 0x0006 directly, the ISR reaches it
 * through the table (see cand/rev22_jmp_via_r2r1.c).
 *
 * The handler does nothing but post event 14 to the main loop. All the actual
 * suspend work -- shutting the ACGs down, blanking the panel, dropping
 * USBCTL.CONN, entering PCON.IDL and re-initialising on wake -- happens outside
 * interrupt context in cmd14_usb_suspend_and_resume (rev22 0x0525), because it
 * parks the CPU in idle mode and must not do that with the USB interrupt still
 * being serviced.
 *
 * These four bytes sit in the gap between the INT0 vector (0x0003, 3 bytes) and
 * the TIMER0 vector (0x000B); Keil packed a leaf function into unused vector
 * space, exactly as it did with the RET stubs above.
 *
 * The event mailbox is IRAM byte 0x0A. Note the 8051 trap: bit address 0x0A is
 * IRAM 0x21 bit 2 and is unrelated. Stock encodes MOV 0x0A,#0x0E (75 0a 0e), a
 * direct-byte move, so 0x0A here is the byte.
 *
 * REV 20 -> REV 22 DELTA: byte-identical, same address. rev20 0x0006 and rev22
 * 0x0006 are both 75 0a 0e 22, and both images' table entry 22 (rev20 @0x0CBF,
 * rev22 @0x0CA9) reads 00 06. Ported from cand/usb_ev_suspend.c verbatim; only
 * the header image/name changed. */

__data __at (0x0A) unsigned char g_event;   /* main-loop event mailbox */

void usb_susr_handler(void) {
    g_event = 14;
}
