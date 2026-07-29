// MATCH: image=rev22 addr=0x0B2E len=9 func=ep0_set_both_dcnt cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Write the value in A to both EP0 data-count registers, IEPDCNTX0 (0xFF6B)
 * and OEPDCNTX0 (0xFFAB). With A == 0 that arms both halves for a zero-length
 * packet (count 0, NAK bit 7 clear); with A == 0x80 it NAKs both.
 *
 * The parameter arrives in A, which is Keil's convention and not one SDCC can
 * express, hence naked assembly.
 *
 * TWO ENTRY POINTS. This is the tail of a slightly longer routine that starts
 * at 0x0B2C with MOVX @DPTR,A / CLR A -- "store the caller's value at the
 * caller's DPTR, then arm both counts to zero". Ghidra names that
 * ep0_store_byte_and_arm_zlp; it is called from 0x0036, 0x0D0D, 0x0FB6 and
 * 0x100E. 0x0B2E is reached directly only from 0x0296, with A already loaded.
 * So 0x0B2C is the source function and 0x0B2E is a merged entry into it; this
 * candidate claims only the 9 bytes assigned to it, and 0x0B2C is left to
 * whoever owns it. Proposed symbols.map row is in proposed/ep0.symbols.
 *
 * REV 20 -> REV 22 DELTA: no Rev 20 counterpart at this boundary. Rev 20
 * spelled the same effect out twice as whole functions with the stores in the
 * other order (ep0_arm_zlp 0x0FEA and the tail of
 * ep0_clear_stall_toggle_and_arm at 0x0B2C, both IN-first and both with a
 * hard-coded zero); Rev 22 factored it into one A-parameterised helper. That
 * is a codegen/factoring change, not a behaviour change -- the same two
 * registers get the same values. */
void ep0_set_both_dcnt(void) __naked {
    __asm
        mov   dptr,#0xff6b      ; IEPDCNTX0
        movx  @dptr,a
        mov   dptr,#0xffab      ; OEPDCNTX0
        movx  @dptr,a
        ret
    __endasm;
}
