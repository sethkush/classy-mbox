// MATCH: image=rev22 addr=0x0D0A len=7 func=oep0_clear_stall_and_rearm cflags=--peep-file,firmware_stock/decomp/keil.peep

/* Rev 22 0x0D0A, 7 bytes: 12 0b 4d  12 0b 2c  22.
 *
 * Recovery path for an EP0 OUT data stage that arrived with nothing armed.
 * The only caller is oep0_int_handler (rev22 0x0CC7), whose first instruction
 * is `JNB 0x0B,0x0D0A`: bit 0x0B is IRAM 0x21.3, "an OUT data stage is
 * expected". If the host sends OUT data we were not waiting for, we do not
 * decode it -- we clear STALL and TOGGLE on both halves of EP0 and re-arm both
 * data counts so the next control transfer starts from a clean state.
 *
 *   LCALL 0x0B4D  ep0_clear_stall_toggle:
 *                   IEPCNF0 &= 0xD7   (store performed)
 *                   OEPCNF0 read, ANL A,#0xD7, and RET with A holding the
 *                   masked value and DPTR still on OEPCNF0 -- the store is
 *                   deliberately left to the caller
 *   LCALL 0x0B2C  ep0_store_byte_and_arm_zlp:
 *                   MOVX @DPTR,A      (that is the OEPCNF0 store)
 *                   CLR A, then falls into ep0_set_both_dcnt (0x0B2E):
 *                   IEPDCNTX0 = 0, OEPDCNTX0 = 0
 *
 * 0xD7 is ~(0x28): bit 5 TOGGLE and bit 3 STALL, per the TAS1020B datasheet
 * 6.4.3.6.1. Compare ep0_clear_stall_both (rev22 0x0B3E), which masks with
 * 0xF7 and so leaves the data toggle alone. Zeroing the two DCNTX0 registers
 * clears both the byte count and the NACK bit (bit 7), so the UBM answers the
 * next token instead of NACKing.
 *
 * ================= REV 20 -> REV 22 DELTA =================
 * THIS FUNCTION DOES NOT EXIST IN REV 20 AS A FUNCTION, but the code does, and
 * the behaviour is identical. In Rev 20 the same two steps are four bytes at
 * the tail of ep0_out_data_handler itself (rev20 0x0D67: `12 0B 1E` / `22`),
 * because Rev 20's 0x0B1E is the MERGED routine -- it does the OEPCNF0 store
 * itself before falling into the arm-both tail, so one call suffices.
 *
 * Rev 22 unmerged that pair (see cand/ep0_clear_stall_toggle_and_arm.c, which
 * predicted exactly this), so the sequence costs seven bytes instead of four,
 * and Keil then factored those seven bytes out into their own function rather
 * than leaving them inline. Net effect on oep0_int_handler: it shrank from 70
 * bytes to 67 and grew an out-of-line tail. No behavioural change at all --
 * the same two register mask-writes and the same two data-count clears happen
 * in the same order.
 *
 * WRITTEN AS ASSEMBLY. Both A and DPTR are live across the boundary between
 * the two callees: 0x0B4D returns with the value to be stored in A and the
 * address in DPTR, and 0x0B2C consumes both. "Call something that leaves DPTR
 * and A live for the next call" has no C spelling, and SDCC would in any case
 * fold the second call and the RET into a tail LJMP, which is one byte short
 * of stock. */
void oep0_clear_stall_and_rearm(void) __naked {
    __asm
        .globl _ep0_clear_stall_toggle
        .globl _ep0_store_byte_and_arm_zlp
        lcall _ep0_clear_stall_toggle      ; rev22 0x0B4D -- leaves A and DPTR live
        lcall _ep0_store_byte_and_arm_zlp  ; rev22 0x0B2C -- stores A, then arms both
        ret
    __endasm;
}
