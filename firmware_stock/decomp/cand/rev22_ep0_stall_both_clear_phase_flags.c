// MATCH: image=rev22 addr=0x1001 len=21 func=ep0_stall_both_clear_phase_flags cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Stall both halves of endpoint 0 -- the protocol response to a control
 * request the firmware does not implement or will not honour. Sets STALL
 * (bit 3) in IEPCNF0 and OEPCNF0, arms both data-count registers for a
 * zero-length packet, and drops any expected data stage.
 *
 * The OUT half is assembly because of Keil's tail merge: stock computes
 * `OEPCNF0 | 8` into A, leaves DPTR pointing at OEPCNF0, and LCALLs 0x0B2C,
 * whose first instruction MOVX @DPTR,A performs the store this function did
 * not, after which CLR A falls through into ep0_set_both_dcnt (0x0B2E) and
 * arms both endpoints. Passing a value in A and DPTR into a callee is not
 * expressible in C.
 *
 * REV 20 -> REV 22 DELTA: the 21 bytes are identical except the merged-tail
 * call operand, 0x0B2B (rev20 ep0_stall_both @ 0x1009) -> 0x0B2C (rev22).
 * Behaviour unchanged.
 *
 * The CALLERS changed, though, and that is a real structural delta. In Rev 20
 * this was the most-called function in the image: eleven LJMP 0x1009 and one
 * LCALL 0x1009. In Rev 22 the only reference to 0x1001 anywhere is a single
 * LCALL from a four-byte trampoline at 0x02EF (12 10 01 22), and every reject
 * path targets the trampoline instead -- LJMP 0x02EF from 0x008E, 0x0100,
 * 0x0114, 0x0124, 0x012A, 0x0159, 0x01E3, 0x022C, 0x0262 and SJMP 0x02EF from
 * 0x029B, 0x02DC, 0x02ED. Three of those are two-byte short jumps, which is
 * what buys the trampoline its keep. */
void ep0_stall_both_clear_phase_flags(void) {
    IEPCNF0 |= 0x08;
    __asm
        .globl _ep0_store_byte_and_arm_zlp
        mov   dptr,#0xffa8      ; OEPCNF0
        movx  a,@dptr
        orl   a,#0x08           ; STALL
        lcall _ep0_store_byte_and_arm_zlp  ; stores A, then arms both to ZLP
    __endasm;
    f_stage_out = 0;
    f_stage_in  = 0;
}
