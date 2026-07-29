// MATCH: image=rev22 addr=0x0E1B len=59 func=panel_state_cycle_A cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Channel A source selector, advanced one step by each qualifying edge on the
 * P3.3 front-panel button (dispatched from p3_edge_poll_dispatch, rev22
 * 0x0F31 / rev20 0x0ED5).
 *
 * Two hidden state bits in IRAM 0x25 -- sa0 = 0x25.0 (bit addr 0x28) and
 * sa1 = 0x25.2 (bit addr 0x2A) -- walk a three-cycle, and each state emits a
 * three-bit panel code into IRAM 0x22 bits 2:0 (bit addrs 0x10/0x11/0x12):
 *
 *      state (sa0,sa1)     code 2:0     source
 *      (0,x) -> (1,1)       0b101        Mic
 *      (1,1) -> (1,0)       0b011        Line
 *      (1,0) -> (0,0)       0b110        Inst
 *
 * That byte is not a register write; it is the payload shiftreg_out8_p1hi
 * (rev22 0x0EFC) clocks out of P1.7 into the panel shift register, so the
 * selection only reaches hardware on the next commit.
 *
 * THE ASYMMETRY IS REAL AND IS WHAT MAKES THE MATCH.  The middle arm re-tests
 * sa0 that the first arm already proved set: stock is
 *     0x0E1B  JB  0x28,0x0E2A     (first arm taken when sa0 == 0)
 *     0x0E2A  JNB 0x28,0x0E3C     <- redundant, sa0 is known 1 here
 *     0x0E2D  JNB 0x2A,0x0E3C
 * Channel B (rev22 panel_state_cycle_B, 0x0E8F) omits that second test and is
 * three bytes shorter for it.  Writing the middle condition as `sa0 && sa1`
 * rather than `sa1` is what reproduces the extra JNB.
 *
 * The three tail statements recompute the derived panel bit
 *      p_derived (0x22.6, bit addr 0x16) = !(f_spdif | f_force)
 * as three independent tests rather than an if/else chain -- that is Keil's
 * codegen, and writing them as three separate `if`s matches it exactly.  The
 * full account of what 0x22.6 means, and of the other writers that
 * short-circuit this rule, lives in cand/shiftreg8_commit.c; it is one
 * write-only output line, not internal state.
 *
 * REV 20 -> REV 22 DELTA: none.  The 59 stock bytes at rev22 0x0E1B are
 * byte-for-byte identical to rev20 0x0E27 (button_a_cycle_3state).  The
 * function contains no address operands at all -- every instruction is a
 * bit-addressed JB/JNB/SETB/CLR plus two SJMPs with relative displacements --
 * so relocation could not have changed it even if the image had shifted, and
 * in fact nothing did.  The source ring, the state encoding and the derived
 * bit are unchanged between revisions.
 */
void panel_state_cycle_A(void) {
    if (!sa0) {                 /* (0,x) -> Mic  */
        sa0 = 1; sa1 = 1;
        pa_src0 = 1; pa_src1 = 0; pa_src2 = 1;
    } else if (sa0 && sa1) {    /* (1,1) -> Line.  The re-test of sa0 here is
                                   stock; channel B does not have it. */
        sa0 = 1; sa1 = 0;
        pa_src0 = 1; pa_src1 = 1; pa_src2 = 0;
    } else {                    /* (1,0) -> Inst */
        sa0 = 0; sa1 = 0;
        pa_src0 = 0; pa_src1 = 1; pa_src2 = 1;
    }
    /* p_derived = !(f_spdif | f_force), emitted as three separate tests. */
    if (!f_spdif) p_derived = 1;
    if (f_spdif)  p_derived = 0;
    if (f_force)  p_derived = 0;
}
