// MATCH: image=rev22 addr=0x024D len=12 func=std_set_address cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void ep0_done_no_data(void);   /* rev22 0x02E8 */

/* SET_ADDRESS.  USB 2.0 9.4.6: the new address must not take effect until
 * after the status stage completes, so the value is stashed in IRAM 0x0E and
 * the EP0-IN completion handler is the one that writes USBFADR.  IRAM 0x0D is
 * the pending-request tag; 5 is SET_ADDRESS, i.e. the same number as
 * bRequest, which is how the completion handler knows what to do.
 *
 * REV 20 -> REV 22: identical behaviour, and the same two stores at the same
 * two IRAM addresses (rev20 0x024D, 14 B).  The only change is the tail:
 * Rev 20 open-coded `f_stage_out = 0; f_stage_in = 0;` (CLR 0x0b / CLR 0x0c /
 * RET) and Rev 22 tail-calls ep0_done_no_data at 0x02E8, which is those same
 * three instructions shared with three other call sites.  Two bytes shorter.
 *
 * The tail call is plain C: SDCC's tail-call peephole turns the trailing
 * `ep0_done_no_data();` + RET into the LJMP stock has, which is the behaviour
 * cand/std_request_unknown_default.c documents from the other direction. */
void std_set_address(void) {
    g_class_tag = 5;                  /* IRAM 0x0D: pending request = SET_ADDRESS */
    g_pending_addr = SETUP_wValueL;   /* IRAM 0x0E: the address, applied later */
    ep0_done_no_data();               /* no data stage; status stage only */
}
