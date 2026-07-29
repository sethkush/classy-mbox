// MATCH: image=rev22 addr=0x0145 len=23 func=std_clear_feature cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void ep0_clear_stall_both(void);   /* rev22 0x0B3E, rev20 0x0B50 */
extern void ep0_done_no_data(void);       /* rev22 0x02E8 — no rev20 equivalent */
extern void ep0_stall_both(void);         /* rev22 0x02EF, rev20 0x1009 */

/* CLEAR_FEATURE.  Same policy as Rev 20 (cand/std_clear_feature.c, 0x0144):
 * only ENDPOINT_HALT on endpoint 0 is honoured — bmRequestType must be
 * exactly 0x02 (standard / host-to-device / endpoint recipient) and wIndex
 * must select EP0.  Neither image looks at wValue, so any feature selector
 * aimed at EP0 clears the stall.
 *
 * The `^ 2` mirrors Keil's XRL A,#2 / JNZ (rev20 0x0148, rev22 0x0149) rather
 * than a CJNE; that idiom is unchanged between the images.
 *
 * WHAT CHANGED IN REV 22 is only the exits.  Rev 20 ended the success path
 * with the two phase flags cleared inline (0x0154 C2 0B / C2 0C / 22).  Rev 22
 * factored that trio into ep0_done_no_data at 0x02E8 and reaches it with
 * LJMP 0x02E8 (0x0156); four call sites share it (0x005E, 0x0156, 0x0256,
 * 0x0299 per the XREF list at rev22 0x02E8).  Likewise the failure path is
 * LJMP 0x02EF into ep0_stall_both, which in Rev 22 is a two-instruction
 * wrapper (LCALL 0x1001 / RET) rather than Rev 20's stall body at 0x1009.
 *
 * Both exits are 3-byte LJMPs, so SDCC's ordinary tail-call collapse reaches
 * them and the body needed no rewriting beyond swapping the two inline flag
 * stores for a call. */
void std_clear_feature(void) {
    if (((SETUP_bmRequestType ^ 2) == 0) && (SETUP_wIndexL == 0)) {
        ep0_clear_stall_both();
        ep0_done_no_data();
        return;
    }
    ep0_stall_both();
}
