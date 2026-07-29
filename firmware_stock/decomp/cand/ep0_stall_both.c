// MATCH: image=rev20 addr=0x1009 len=21 func=ep0_stall_both cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Stall both halves of endpoint 0 -- the protocol response to a control
 * request the firmware does not implement or will not honour. Sets STALL
 * (bit 3) in IEPCNF0 and OEPCNF0, arms both data-count registers for a
 * zero-length packet, and drops any expected data stage.
 *
 * This is the most-called function in the image: twelve XREFs in Rev 20, one
 * per request handler's reject path. A byte scan of rev20_firmware_code.bin
 * finds 02 10 09 (LJMP 0x1009) at 0x0092, 0x010A, 0x015A, 0x01E8, 0x022C,
 * 0x0264, 0x0299, 0x029C, 0x02DE, 0x02E4 and 0x02E7, and 12 10 09
 * (LCALL 0x1009) at 0x02EA -- eleven jumps and one call. All twelve are
 * instruction-aligned, not accidental matches straddling a boundary: each
 * appears as its own three-byte instruction in the Ghidra listing
 * (firmware_stock/disasm/rev20_ghidra.txt).
 *
 * The OUT half is written in assembly because of Keil's tail merge. Stock
 * computes `OEPCNF0 | 8` into A, leaves DPTR pointing at OEPCNF0, and then
 * LCALLs 0x0B2B -- which is the middle of ep0_clear_stall_toggle_and_arm, not
 * a function of its own. Its first instruction, `MOVX @DPTR,A`, performs the
 * store this function did not, and the rest arms both endpoints. Passing a
 * value in A and DPTR into a callee is not expressible in C, so the call site
 * is spelled out; see cand/ep0_clear_stall_toggle_and_arm.c for the span.
 *
 * Rev 22 has the same body at 0x1001 (ep0_stall_both_clear_phase_flags),
 * byte-identical apart from the merged-tail target moving to 0x0B2C, and adds
 * a four-byte trampoline at 0x02EF (12 10 01 22 = LCALL 0x1001 / RET) that
 * every caller uses instead: that LCALL is the ONLY reference to 0x1001 in
 * rev22, and it is reached by LJMP 0x02EF from 0x008E, 0x0100, 0x0114, 0x0124,
 * 0x012A, 0x0159, 0x01E3, 0x022C and 0x0262, and by two-byte SJMP 0x02EF from
 * 0x029B, 0x02DC and 0x02ED. */
void ep0_stall_both(void) {
    IEPCNF0 |= 0x08;
    __asm
        .globl _ep0_store_cnf_and_arm_both
        mov   dptr,#0xffa8      ; OEPCNF0
        movx  a,@dptr
        orl   a,#0x08           ; STALL
        lcall _ep0_store_cnf_and_arm_both   ; stores A, then arms both to ZLP
    __endasm;
    f_stage_out = 0;
    f_stage_in  = 0;
}
