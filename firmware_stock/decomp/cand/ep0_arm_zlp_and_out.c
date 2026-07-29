// MATCH: image=rev20 addr=0x0B82 len=10 func=ep0_arm_zlp_and_out cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Arm both halves of endpoint 0 for a zero-length packet: writing 0 to the
 * data-count register clears both the byte count and the NAK bit (7), so the
 * UBM answers the next token with a 0-byte packet instead of NAKing.
 *
 * Identical in effect to ep0_arm_zlp (Rev 20 0x0FEA), and the same
 * instructions -- only the order of the two stores differs, OUT first here and
 * IN first there. Two source functions that were never folded together.
 *
 * Rev 22 keeps this one verbatim at 0x0B75 (same ten bytes,
 * 90 ff ab e4 f0 90 ff 6b f0 22). It does NOT keep the IN-first twin: the
 * byte string 90 ff 6b e4 f0 90 ff ab f0 22 occurs twice in Rev 20 (0x0B2C
 * and 0x0FEA) and zero times in Rev 22, which instead reaches the same effect
 * through ep0_set_both_dcnt at Rev 22 0x0B2E. */
void ep0_arm_zlp_and_out(void) {
    OEPDCNTX0 = 0;
    IEPDCNTX0 = 0;
}
