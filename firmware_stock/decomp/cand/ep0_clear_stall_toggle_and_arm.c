// MATCH: image=rev20 addr=0x0B1E len=24 func=ep0_clear_stall_toggle_and_arm span=1 cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Recover endpoint 0 after an OUT data stage that arrived with nothing armed:
 * clear STALL (bit 3) and TOGGLE (bit 5) on both halves of the EP0 configuration
 * registers, then write 0 to both data-count registers, which clears the byte
 * count and the NAK bit (7) so the UBM answers the next token with a
 * zero-length packet.
 *
 * 0xD7 == ~(0x28): bit 5 TOGGLE and bit 3 STALL. Compare ep0_clear_stall_both
 * (0x0B50), which uses 0xF7 and so leaves the data toggle alone.
 *
 * SPAN. Ghidra splits this at 0x0B2B, calling the second half
 * `ep0_store_cnf_and_arm_both`, because that address has its own callers
 * (0x0036, 0x0FD5, and ep0_stall_both at 0x1016). It is not a separate source
 * function: the `MOVX @DPTR,A` at 0x0B2B is the store half of this function's
 * own `OEPCNF0 &= 0xD7`, and the callers jump in with A holding the value they
 * want stored and DPTR still pointing at OEPCNF0. That is Keil tail merging,
 * the same shape as dptr_from_ep0_ptr inside dptr_to_ep0_out_buf, so the whole
 * 24-byte run 0x0B1E..0x0B35 is claimed here and 0x0B2B is proposed for
 * symbols.map as an entry point.
 *
 * Rev 22 is the proof of the modelling. There the merge is gone: 0x0B4D is
 * this function minus the final store, ending `MOVX A,@DPTR / ANL A,#0xD7 /
 * RET` at 0x0B5A with A live, and every caller follows it with a second
 * `LCALL 0x0B2C` to the store-and-arm half -- see Rev 22 0x0D0A/0x0D0D and
 * 0x0FB3/0x0FB6. Two callees at source level, merged by Keil in Rev 20 and
 * left unmerged in Rev 22. */
void ep0_clear_stall_toggle_and_arm(void) {
    IEPCNF0 &= 0xD7;
    OEPCNF0 &= 0xD7;
    /* ---- entry point ep0_store_cnf_and_arm_both, stock 0x0B2B ---- */
    IEPDCNTX0 = 0;
    OEPDCNTX0 = 0;
}
