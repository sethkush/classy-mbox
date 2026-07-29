// MATCH: image=rev22 addr=0x0B4D len=14 func=ep0_clear_stall_toggle cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Clear STALL (bit 3) and TOGGLE (bit 5) on both halves of the EP0
 * configuration registers -- 0xD7 == ~0x28. Resetting the data toggle as well
 * as the stall is the recovery used when a new SETUP arrives while an old
 * data stage was still outstanding, since the toggle sequence restarts at
 * DATA1 for the next stage.
 *
 * IT RETURNS WITHOUT STORING THE SECOND VALUE. The last three instructions are
 * MOVX A,@DPTR / ANL A,#0xD7 / RET, with DPTR still on OEPCNF0 (0xFFA8) and
 * the masked value live in A. That is not a bug: every caller immediately does
 * a second LCALL 0x0B2C (ep0_store_byte_and_arm_zlp), whose MOVX @DPTR,A
 * completes the store and then arms both data-count registers. See the caller
 * pairs at rev22 0x0D0A/0x0D0D and 0x0FB3/0x0FB6.
 *
 * REV 20 -> REV 22 DELTA: STRUCTURAL, and it runs the opposite way to the
 * usual direction of travel. In Rev 20 this and the store-and-arm tail were
 * ONE function, ep0_clear_stall_toggle_and_arm at 0x0B1E, 24 bytes
 * (0x0B1E..0x0B35), whose tail from 0x0B29 is
 * 54 d7 f0 90 ff 6b e4 f0 90 ff ab f0 22 -- Keil had merged the tail so
 * that 0x0B2B was an entry point into it. Rev 22 leaves them unmerged: the
 * same source is emitted as two callees and the caller makes two calls. The
 * effect on the wire is identical; the only difference is code size and where
 * the boundary falls. This is direct evidence that the Rev 20 span modelling
 * in cand/ep0_clear_stall_toggle_and_arm.c was right.
 *
 * Naked assembly: a C function cannot end with a value live in A and DPTR. */
void ep0_clear_stall_toggle(void) __naked {
    __asm
        mov   dptr,#0xff68      ; IEPCNF0
        movx  a,@dptr
        anl   a,#0xd7           ; ~(TOGGLE | STALL)
        movx  @dptr,a
        mov   dptr,#0xffa8      ; OEPCNF0
        movx  a,@dptr
        anl   a,#0xd7
        ret                     ; A and DPTR live -- caller stores them
    __endasm;
}
