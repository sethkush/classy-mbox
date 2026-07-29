// MATCH: image=rev22 addr=0x0B75 len=10 func=ep0_flush_arm cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Arm both halves of endpoint 0 for a zero-length packet. Writing 0 to a
 * data-count register clears both the byte count and the NAK bit (7), so the
 * UBM answers the next token with a 0-byte packet instead of NAKing -- the
 * status stage of a control transfer that carried no data. OUT is written
 * first, then IN.
 *
 * REV 20 -> REV 22 DELTA: byte-identical to rev20 ep0_arm_zlp_and_out at
 * 0x0B82 (90 ff ab e4 f0 90 ff 6b f0 22). What did NOT survive is Rev 20's
 * IN-first twin: the string 90 ff 6b e4 f0 90 ff ab f0 22 occurs twice in
 * Rev 20 (0x0B2C, 0x0FEA) and zero times in Rev 22, which reaches the same
 * effect through ep0_set_both_dcnt (0x0B2E) instead. */
void ep0_flush_arm(void) {
    OEPDCNTX0 = 0;
    IEPDCNTX0 = 0;
}
