// MATCH: image=rev22 addr=0x0B2C len=2 func=ep0_store_byte_and_arm_zlp cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Two bytes -- MOVX @DPTR,A / CLR A -- that fall through into
 * ep0_set_both_dcnt at 0x0B2E (cand/rev22_ep0_set_both_dcnt.c).
 *
 * Read as one routine, 0x0B2C..0x0B36 is: "store the caller's byte at the
 * caller's DPTR, then zero both EP0 data-count registers, IEPDCNTX0 (0xFF6B)
 * and OEPDCNTX0 (0xFFAB)". Zeroing the counts with bit 7 clear arms both halves
 * of EP0 for a zero-length packet, so every caller is finishing an EP0 write to
 * an endpoint-configuration register and then arming the status stage in one
 * go. Both the value and the destination arrive in registers -- A and DPTR --
 * which is Keil's calling convention and not one SDCC can express, so this is
 * naked assembly. There is no RET here: control runs on into 0x0B2E, whose RET
 * serves both entry points.
 *
 * The four callers all fit that description (Ghidra XREFs on 0x0B2C):
 *
 *   0x0036  usb_setup_handler, after a read-modify-write of an EP0 config reg
 *   0x0D0D  oep0_clear_stall_and_rearm
 *   0x0FB6  ep0_in_done_handler
 *   0x100E  ep0_stall_both_clear_phase_flags, storing OEPCNFG_0 (0xFFA8) with
 *           the STALL bit just ORed in
 *
 * WHY IT IS ITS OWN CANDIDATE. Ghidra lists 0x0B2C and 0x0B2E as two functions
 * because both have callers; in the source they are one function with a merged
 * entry point at 0x0B2E (reached only from 0x0296 with A preloaded to 0x80, to
 * NAK both halves instead of arming them). The candidate split follows the
 * batch split rather than the source structure: 0x0B2E belongs to another
 * batch, so this file claims the two-byte prologue and nothing else. The
 * matching symbols.map row is proposed in proposed/stubs.symbols.
 *
 * REV 20 -> REV 22 DELTA: no direct counterpart, and this is a factoring change
 * rather than a behavioural one. Rev 20 has the same two bytes in the same
 * role at 0x0B2B -- also MOVX @DPTR,A also falling into a
 * "write the EP0 data counts" tail -- but there the tail is hard-coded
 * (MOV DPTR,#0xFF6B / CLR A / MOVX / MOV DPTR,#0xFFAB / MOVX / RET), so the
 * CLR A sits *after* the first DPTR load and Rev 20 cannot enter it with a
 * caller-supplied value. Rev 20 therefore spells the NAK case out as a whole
 * separate function, ep0_nack_both at 0x0B5F (MOV DPTR,#0xFF6B / MOV A,#0x80 /
 * MOVX / MOV DPTR,#0xFFAB / MOVX / CLR 0x0B / CLR 0x0C / RET). Rev 22 hoisted
 * the CLR A up into this prologue, which turns the tail into an
 * A-parameterised helper, and the 0x0296 site then reaches it with A = 0x80 and
 * gets the flag clears from ep0_done_no_data (SJMP 0x02E8) instead. Same
 * register writes with the same values in both images.
 *
 * Rev 20's 0x0B2B is covered by cand/ep0_clear_stall_toggle_and_arm.c
 * (addr=0x0B1E len=24 span=1), which is why it needs no candidate of its own
 * there and this one does here -- Rev 22 split that Rev 20 function in two
 * (rev22 ep0_clear_stall_toggle is at 0x0B4D, no longer adjacent). */
void ep0_store_byte_and_arm_zlp(void) __naked {
    __asm
        movx  @dptr,a           ; store the caller's byte at the caller's DPTR
        clr   a                 ; ...then fall into ep0_set_both_dcnt with A = 0,
                                ;    arming both EP0 halves for a ZLP
    __endasm;
}
