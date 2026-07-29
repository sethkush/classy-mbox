// MATCH: image=rev22 addr=0x0E8F len=56 func=panel_state_cycle_B cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Channel B source selector, advanced by the P3.4 front-panel button
 * (dispatched from p3_edge_poll_dispatch, rev22 0x0F31 / rev20 0x0ED5).
 *
 * Identical machine to channel A (rev22 panel_state_cycle_A, 0x0E1B) shifted
 * to its own bits: hidden state sb0 = IRAM 0x25.1 (bit addr 0x29) and
 * sb1 = 0x25.3 (bit addr 0x2B), panel code in IRAM 0x22 bits 5:3 (bit addrs
 * 0x13/0x14/0x15), same ring 0b101 Mic -> 0b011 Line -> 0b110 Inst.
 *
 * The one difference from channel A is the missing redundant test.  Stock
 * enters the middle arm with a single
 *     0x0E8F  JB  0x29,0x0E9E
 *     0x0E9E  JNB 0x2B,0x0EAD      <- tests sb1 only
 * where channel A re-tests its own state bit first.  Three bytes shorter, and
 * behaviourally the same because sb0 is provably 1 at that point.  The C
 * therefore says `if (sb1)`, not `if (sb0 && sb1)`.
 *
 * The shared tail recomputes p_derived (IRAM 0x22.6, bit addr 0x16) as
 * !(f_spdif | f_force) in three independent tests, exactly as channel A does;
 * see cand/shiftreg8_commit.c for the full account of that bit.
 *
 * REV 20 -> REV 22 DELTA: none.  The 56 stock bytes at rev22 0x0E8F are
 * byte-for-byte identical to rev20 0x0E9D (button_b_cycle_3state).  As with
 * channel A there are no address operands to relocate.
 */
void panel_state_cycle_B(void) {
    if (!sb0) {                 /* (0,x) -> Mic  */
        sb0 = 1; sb1 = 1;
        pb_src0 = 1; pb_src1 = 0; pb_src2 = 1;
    } else if (sb1) {           /* (1,1) -> Line -- no re-test of sb0 */
        sb0 = 1; sb1 = 0;
        pb_src0 = 1; pb_src1 = 1; pb_src2 = 0;
    } else {                    /* (1,0) -> Inst */
        sb0 = 0; sb1 = 0;
        pb_src0 = 0; pb_src1 = 1; pb_src2 = 1;
    }
    if (!f_spdif) p_derived = 1;
    if (f_spdif)  p_derived = 0;
    if (f_force)  p_derived = 0;
}
