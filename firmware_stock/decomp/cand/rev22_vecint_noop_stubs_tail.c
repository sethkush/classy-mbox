// MATCH: image=rev22 addr=0x1029 len=13 span=1 func=vecint_iep7_noop cflags=--peep-file,firmware_stock/decomp/keil.peep
/* The thirteen VECINT no-op handlers that did not fit in the vector-table gaps
 * (see cand/rev22_vecint_noop_stubs.c). They sit at the top of the image, right
 * after toggle_bit1E_state ends at 0x1029; 0x1036 onward is 0xFF erase fill, so
 * these are the last code bytes in Rev 22.
 *
 * Ghidra lists thirteen one-byte functions here. This candidate claims the run
 * with span=1, matching cand/vecint_noop_stubs_tail.c in the Rev 20 batch.
 *
 * Each is the target of one or more entries of the VECINT dispatch table at
 * 0x0C7D (37 big-endian code addresses, 0x0C7D..0x0CC6). The entry index below
 * is (siteAddr - 0x0C7D)/2, read out of the image bytes; Ghidra records the
 * same links as XREFs on each stub. 0x1035 is shared by six entries -- the
 * "definitely nothing to do here" address.
 *
 * ================= THE REV 22 FIX, IN THESE BYTES =================
 *
 * This block is where the one behavioural change in the vector machinery shows
 * up. Both dispatch tables were read out of the images in full and compared
 * entry by entry. Thirty-one of the thirty-seven entries differ, but thirty of
 * those thirty differences are pure relocation. Exactly one is not.
 *
 *   ENTRY 20 = VECINT 0x14 = SOF_INT (USB start-of-frame)
 *       rev20 @0x0CBB -> 0x1034   a RET stub in this block
 *       rev22 @0x0CA5 -> 0x0D58   sof_int_handler, a 70-byte routine
 *
 * That is the single vector-table change of substance: Rev 20 ships NO
 * start-of-frame handler; Rev 22 puts a real one in the slot.
 *
 * The rest of the block corroborates it arithmetically rather than by
 * assertion. Rev 20's block is 0x1031..0x103E, fourteen bytes; Rev 22's is
 * 0x1029..0x1035, thirteen -- one fewer, because the SOF stub is gone. Listing
 * both in table order:
 *
 *     entry  rev20   rev22        entry  rev20   rev22
 *      15    0x1031  0x1029        29    0x1039  0x1030
 *      16    0x1032  0x102A        31    0x103A  0x1031
 *      19    0x1033  0x102B        32    0x103B  0x1032
 *      20    0x1034  0x0D58 <--    33    0x103C  0x1033
 *      21    0x1035  0x102C        36    0x103D  0x1034
 *      24    0x1036  0x102D      shared  0x103E  0x1035
 *      25    0x1037  0x102E
 *      28    0x1038  0x102F
 *
 * The stubs before the deleted one shift by exactly -8 (the block's move);
 * every stub after it shifts by -9. One extra byte of displacement, appearing
 * precisely at the SOF slot and nowhere else. So the SOF stub was not moved or
 * repurposed -- it was removed, and its table entry repointed at real code.
 *
 * WHAT IT MEANS. SOF is not masked off in either image: USBIMSK (XDATA 0xFFFD)
 * is loaded with 0x9F (rev22 0x0910, rev20 0x09EF) and 0x9F = 1001 1111 has
 * bit 4, SOF, set; the bus-reset handler rewrites the same 0x9F, and starting
 * a stream raises the mask to 0xFF (rev22 0x03F8, rev20 0x03F4), which also
 * leaves SOF set. So in Rev 20 the SOF interrupt is UNMASKED and fires once per
 * millisecond, every frame: it enters the ISR, indexes the table, LCALLs
 * 0x1034, and immediately RETs. The USB frame clock was being delivered to
 * firmware and thrown away. Rev 22 is the image that does something with it.
 *
 * What sof_int_handler at 0x0D58 actually does is not decoded here -- it is
 * another batch's function and I have not read its bytes, so I make no claim
 * about its contents beyond its address and 70-byte extent.
 *
 * VECINT names are TI's (reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h,
 * "interrupt source"); 0x11 and 0x1E are marked reserved there and 0x20..0x23
 * are not defined at all, which is why those four share the do-nothing stub.
 * Since the table index IS the VECINT value, index and name are the same fact.
 */
void vecint_iep7_noop(void) __naked {
    __asm
        ret                        ; 0x1029  <- entry 15 @ 0x0C9B  IEP7_INT
        ret                        ; 0x102A  <- entry 16 @ 0x0C9D  STPOW_INT
        ret                        ; 0x102B  <- entry 19 @ 0x0CA3  PSOF_INT
                                   ;          (pre-SOF; still a no-op in rev22)
                                   ;  --- entry 20, SOF_INT, had its stub HERE
                                   ;      in rev20 (0x1034) and is gone: rev22
                                   ;      entry 20 @0x0CA5 points at 0x0D58 ---
        ret                        ; 0x102C  <- entry 21 @ 0x0CA7  RESR_INT
        ret                        ; 0x102D  <- entry 24 @ 0x0CAD  CPRX_INT
        ret                        ; 0x102E  <- entry 25 @ 0x0CAF  CPTX_INT
        ret                        ; 0x102F  <- entry 28 @ 0x0CB5  I2CRX_INT
        ret                        ; 0x1030  <- entry 29 @ 0x0CB7  I2CTX_INT
        ret                        ; 0x1031  <- entry 31 @ 0x0CBB  XINT_INT
        ret                        ; 0x1032  <- entry 32 @ 0x0CBD  (0x20, undefined)
        ret                        ; 0x1033  <- entry 33 @ 0x0CBF  (0x21, undefined)
        ret                        ; 0x1034  <- entry 36 @ 0x0CC5  NO_INT
        ret                        ; 0x1035  <- entries 17, 26, 27, 30, 34, 35
                                   ;            = 0x11 reserved, DPRX_INT,
                                   ;              DPTX_INT, 0x1E reserved,
                                   ;              0x22, 0x23
                                   ;            @ 0x0C9F 0x0CB1 0x0CB3 0x0CB9
                                   ;              0x0CC1 0x0CC3
    __endasm;
}
