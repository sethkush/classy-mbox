// MATCH: image=rev20 addr=0x01F1 len=62 func=std_get_interface cflags=--peep-file,firmware_stock/decomp/keil.peep partial=3 at=0x12
#include "mbox.h"
extern void ep0_ptr_set_in_buf(void);
extern void ep0_send_1byte(void);
extern void ep0_stall_both(void);
__bit __at (0x08) f_iface1_alt;
__bit __at (0x09) f_iface2_alt;
__bit __at (0x0A) f_cfg_alt;     /* written but never set — vestigial */
__bit __at (0x0E) f_configured;

/* GET_INTERFACE: reply with one byte, the current alternate setting.
 *
 * All three arms are reachable. The guard rejects wIndex > 2, not >= 2:
 * `SETB C / SUBB A,#2 / JNC` borrows when A < 3, so wIndex 0, 1 and 2 all get
 * through. (std_set_configuration does carry a genuinely dead third arm, but
 * that is a different function and a different constant -- do not read this
 * one as the same habit.)
 *
 * f_cfg_alt is never set anywhere in the image, so the first test always falls
 * through to the f_configured check. It is written by std_set_configuration
 * and read here and nowhere else — vestigial, but it costs three bytes that
 * have to be reproduced. */
void std_get_interface(void) {
    /* Both rejections jump to one shared LJMP at the end -- stock has a single
     * `LJMP ep0_stall_both` at 0x022C with two XREFs. Writing `{ stall;
     * return; }` inline instead makes SDCC emit its own exit per test and
     * invert the bit tests; the goto reproduces the shared tail and, with the
     * short-circuit &&, the JB/JNB pair Keil emitted. */
    if (!f_cfg_alt && !f_configured) goto stall;
    if (SETUP_wIndexL > 2) goto stall;

    /* PARTIAL, 3 bytes. Stock reads SETUP_wIndexL here with a bare
     * `MOVX A,@DPTR`: DPTR still holds 0xFF2C from the range check above,
     * because ep0_ptr_set_in_buf touches only IRAM 0x1B and 0x1C and Keil's
     * inter-procedural register analysis knew it. SDCC has no such analysis
     * and reloads DPTR, costing three bytes.
     *
     * Everything else in this function matches. The gap is not a modelling
     * error and no rewrite of the C closes it -- it is a compiler capability
     * SDCC does not have. Left as readable C rather than hand assembly, and
     * declared above so the shortfall is counted rather than hidden. */
    ep0_ptr_set_in_buf();
    if (SETUP_wIndexL == 1 && f_iface1_alt) {
        __asm
            .globl _dptr_from_ep0_ptr
            lcall _dptr_from_ep0_ptr
            mov   a,#0x01
            movx  @dptr,a
        __endasm;
    } else if (SETUP_wIndexL == 2 && f_iface2_alt) {
        __asm
            lcall _dptr_from_ep0_ptr
            mov   a,#0x02
            movx  @dptr,a
        __endasm;
    } else {
        __asm
            lcall _dptr_from_ep0_ptr
            clr   a
            movx  @dptr,a
        __endasm;
    }
    ep0_send_1byte();
    return;
stall:
    ep0_stall_both();
}
