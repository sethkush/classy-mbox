// MATCH: image=rev22 addr=0x0F91 len=41 func=ep0_in_done_handler cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"

/* VECINT 0x08 (IEP0_INT) -- the UBM finished sending an EP0 IN packet.
 * Rev 22 at 0x0F91. Table entry 0x08 sits at 0x0C7D + 2*8 = 0x0C8D and reads
 * 0F 91, which is the XREF Ghidra shows. (rev20: handler 0x0FC4, entry 0x0CA3.)
 *
 * NOTE ON LENGTH. The batch sheet gives 45 bytes; the function is 41. The RET
 * is at 0x0FB9, so 0x0F91..0x0FB9 inclusive is 0x29 = 41 bytes, and the Ghidra
 * listing has no instruction between 0x0FB9 and the next named function. The
 * four bytes at 0x0FBA (01 22 00 01) are the head of an unlisted data region
 * running to 0x0FDD; they are not part of this function and the header claims
 * 41.
 *
 * ================= WHAT IT DOES =================
 * Three mutually exclusive cases, in the order stock tests them:
 *
 *  1. bit 0x0B (IRAM 0x21.3) set -- more of a multi-packet IN transfer is
 *     still queued. Refill and re-arm via ep0_in_stage_and_go (0x0B63).
 *
 *  2. bit 0x0C (IRAM 0x21.4) set -- that was the LAST data packet, so the OUT
 *     status stage is next. Clear the flag and set TOGGLE (bit 5) in OEPCNF0
 *     so the zero-length status handshake is expected as DATA1, then arm both
 *     halves.
 *
 *  3. neither -- the packet just sent WAS the status stage of a request with
 *     no data. If that request was SET_ADDRESS (tag 5 in IRAM BYTE 0x0D), this
 *     is the moment the new address becomes legal: USB 2.0 9.4.6 requires the
 *     device to keep answering on the old address until the status stage
 *     completes, so std_set_address only stashed the value in IRAM 0x0E and
 *     tagged 0x0D, and USBFADR is written here.
 *
 * 8051 trap, worth repeating: `MOV A,0x0D` at 0x0FA5 reads IRAM BYTE 0x0D, the
 * request tag. The bit-addressed 0x0D that ep0_clamp_len_to_wlength clears at
 * 0x0DC2 is a different object entirely, IRAM 0x21.5.
 *
 * ================= REV 20 -> REV 22 DELTA =================
 * NO BEHAVIOURAL CHANGE. Same three cases, same order, same tag 5, same
 * OEPCNF0 TOGGLE, same USBFADR write. Rev 20 is 38 bytes at 0x0FC4, Rev 22 is
 * 41 at 0x0F91, and every difference is a consequence of Rev 22 having
 * unmerged the clear-stall/arm pair (see cand/ep0_clear_stall_toggle_and_arm.c):
 *
 *     case 1 tail   rev20 `02 0B 77` LJMP ep0_in_start_transfer
 *                   rev22 `12 0B 63` LCALL ep0_in_stage_and_go + `22` RET   +1
 *     case 2 tail   rev20 `02 0B 2B` LJMP into the merged store-and-arm tail
 *                   rev22 `80 11`    SJMP 0x0FB6, the local LCALL 0x0B2C     -1
 *     case 3 tail   rev20 `12 0B 1E` LCALL merged clear-stall-toggle-and-arm
 *                   rev22 `12 0B 4D` + `12 0B 2C`, two calls                +3
 *
 * 38 + 1 - 1 + 3 = 41. The JNB displacements move with the bodies (0x03->0x04,
 * 0x0B->0x0A) and are not independent changes.
 *
 * CORRECTION TO A CLAIM ON RECORD. cand/usb_iep0_done_handler.c (the Rev 20
 * candidate) states that in Rev 22 the `OEPCNF0 | 0x20` at 0x0F9D..0x0FA1 is
 * dead code because 0x0B4D reloads DPTR and A. That is wrong, and the SJMP is
 * the proof: case 2 does not fall through to 0x0FB3, it jumps over it to
 * 0x0FB6, whose `LCALL 0x0B2C` begins `MOVX @DPTR,A`. DPTR is still 0xFFA8 and
 * A still holds OEPCNF0 | 0x20, so the store happens exactly as it does in
 * Rev 20. I verified this by decoding rev22_firmware_code.bin[0x0F91:0x0FBA]
 * byte by byte: `80 11` at 0x0FA3 is SJMP +0x11 from 0x0FA5, i.e. 0x0FB6, and
 * 0x0FB3 is only reached by falling out of case 3. The forced OUT toggle is
 * present in both images.
 *
 * ================= HOW IT IS WRITTEN =================
 * Case 1's `LCALL / RET` is spelled in asm because SDCC folds a C tail call
 * into LJMP, which is Rev 20's encoding, not Rev 22's -- this is the same
 * one-byte flip that rev22_std_get_descriptor.c documents, and it goes the
 * same direction here.
 *
 * Case 2 ends by jumping into case 3's own tail with A and DPTR live. That is
 * a shared tail inside one function, not a call, so it is written as a goto
 * into a label placed between the two LCALLs -- exactly the structure the
 * bytes have.
 *
 * PEEPHOLE DEPENDENCY, inherited from the Rev 20 candidate. The
 * `if (f_stage_in) { f_stage_in = 0; ... }` at 0x0F98 is where SDCC folds test
 * and clear into a single JBC and inverts the branch, while Keil emitted
 * JNB-to-else followed by CLR. Matching depends on the GENERALISED JBC row in
 * firmware_stock/decomp/keil.peep -- the one whose window ends at a label
 * rather than at a RET. Both encodings are 5 bytes, so this is instruction
 * shape, not size. No new rule was needed for Rev 22. */

extern void ep0_in_stage_and_go(void);        /* rev22 0x0B63, rev20 0x0B77 */
extern void ep0_clear_stall_toggle(void);     /* rev22 0x0B4D; rev20's 0x0B1E
                                               * is the merged variant */
extern void ep0_store_byte_and_arm_zlp(void); /* rev22 0x0B2C, = rev20 0x0B2B */

void ep0_in_done_handler(void) {
    __asm
        .globl _ep0_in_stage_and_go
        .globl _ep0_clear_stall_toggle
        .globl _ep0_store_byte_and_arm_zlp
    __endasm;

    if (f_stage_out) {              /* bit 0x0B: more IN data still queued */
        __asm
            lcall _ep0_in_stage_and_go   ; stock LCALL+RET, not a tail LJMP
        __endasm;
        return;
    }

    if (f_stage_in) {               /* bit 0x0C: that was the last data packet */
        f_stage_in = 0;
        /* Set TOGGLE so the OUT status stage is expected as DATA1, then jump
         * into the shared tail below with A holding the new OEPCNF0 value and
         * DPTR still pointing at it -- 0x0B2C's first instruction is the
         * MOVX @DPTR,A that stores it. Not expressible as a C assignment,
         * because the store belongs to the callee. */
        __asm
            mov   dptr,#0xffa8      ; OEPCNF0
            movx  a,@dptr
            orl   a,#0x20           ; TOGGLE
        __endasm;
        goto store_and_arm;
    }

    if (g_class_tag == 5) {         /* SET_ADDRESS status stage completed */
        USBFADR = g_pending_addr;
        g_class_tag = 0;
    }
    __asm
        lcall _ep0_clear_stall_toggle   ; leaves A and DPTR live for the next
    __endasm;

store_and_arm:
    __asm
        lcall _ep0_store_byte_and_arm_zlp
    __endasm;
}
