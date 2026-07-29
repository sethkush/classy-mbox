// MATCH: image=rev20 addr=0x0FC4 len=38 func=usb_iep0_done_handler cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void ep0_in_start_transfer(void);
extern void ep0_clear_stall_toggle_and_arm(void);

/* VECINT 0x08 (IEP0) -- the UBM finished sending an EP0 IN packet.
 * Table entry 0x08 at 0x0CA3 reads 0F C4, which is the XREF Ghidra shows.
 *
 * Three mutually exclusive cases, in the order stock tests them:
 *
 *  1. bit 0x0B set -- more of a multi-packet IN transfer is still queued.
 *     ep0_in_fill_chunk (0x0B8C) sets this bit at 0x0BD0 whenever it stopped
 *     with the length counters IRAM 0x09:0x0B non-zero. Refill and re-arm.
 *
 *  2. bit 0x0C set -- that was the LAST data packet, so the OUT status stage
 *     is next. Clear the flag and set OEPCNF0 bit 5 (TOGGLE) so the status
 *     handshake is expected as DATA1, then arm both halves.
 *
 *  3. neither -- the packet just sent WAS the status stage of a request with
 *     no data. If that request was SET_ADDRESS (tag 5), this is the moment the
 *     new address becomes legal: USB 2.0 9.4.6 requires the device to keep
 *     answering on the old address until the status stage completes, so
 *     std_set_address only stashed the value in IRAM 0x0E and tagged IRAM 0x0D,
 *     and USBFADR is written here.
 *
 * Note the two different 0x0D at 0x0FC4+0x14: `MOV A,0x0d` reads IRAM BYTE
 * 0x0D, the class/request tag. The bit-addressed 0x0D that ep0_in_fill_chunk
 * tests at 0x0BE1 is a different thing entirely (IRAM 0x21.5, the
 * send-a-terminating-ZLP flag).
 *
 * Cases 1 and 2 leave by jumping into another function rather than returning:
 * 0x0FC7 LJMPs to ep0_in_start_transfer, and 0x0FD5 LJMPs to 0x0B2B with A
 * already holding the value to store and DPTR still on OEPCNF0. 0x0B2B is the
 * merged tail of ep0_clear_stall_toggle_and_arm (see that candidate), so the
 * store belongs to the callee -- which is why the `OEPCNF0 |= 0x20` here is
 * written as inline assembly ending in the jump, with no MOVX of its own.
 * Case 3 does NOT tail-jump: stock spends LCALL+RET at 0x0FE6/0x0FE9 where a
 * 3-byte LJMP would have done, so the trailing call is written as inline asm
 * to stop SDCC's lcall/ret -> ljmp peephole from "improving" it.
 *
 * PEEPHOLE DEPENDENCY. The `if (f_stage_in) { f_stage_in = 0; ... }` at 0x0FCA
 * is where SDCC folds the test and the clear into a single JBC and inverts the
 * branch, while Keil emitted JNB-to-else followed by CLR. Matching therefore
 * depends on the GENERALISED JBC row in firmware_stock/decomp/keil.peep -- the
 * one whose window ends at the label rather than at a RET (`jbc %1,%2 / sjmp
 * %3 / %2:` -> `jnb %1,%3 / clr %1`). The older RET-specific JBC row above it
 * does not fire here, because the guarded block is the OEPCNF0 TOGGLE set. Both
 * encodings are 5 bytes, so this is instruction shape, not size. That rule now
 * lives in the committed keil.peep and the cflags on line 1 name no other file.
 *
 * Rev 22 restructured this handler (0x0F91, 34 bytes). Its case-2 arm computes
 * the same `OEPCNF0 | 0x20` at 0x0F9C..0x0F9F and then SJMPs to a shared tail
 * at 0x0FB3 which begins `LCALL 0x0B4D`, and Rev 22's 0x0B4D starts
 * `MOV DPTR,#0xFF68 / MOVX A,@DPTR`, so both A and DPTR are overwritten before
 * anything stores them: in Rev 22 that OR is dead code and the OUT toggle is
 * no longer forced ahead of the status stage. Rev 20 does perform the store. */
void usb_iep0_done_handler(void) {
    if (f_stage_out) {              /* bit 0x0B: more IN data queued */
        ep0_in_start_transfer();
        return;
    }
    if (f_stage_in) {               /* bit 0x0C: last data packet went out */
        f_stage_in = 0;
        __asm
            .globl _ep0_store_cnf_and_arm_both
            mov   dptr,#0xffa8      ; OEPCNF0
            movx  a,@dptr
            orl   a,#0x20           ; TOGGLE: status stage arrives as DATA1
            ljmp  _ep0_store_cnf_and_arm_both   ; stores A, then arms both
        __endasm;
    }
    if (g_class_tag == 5) {         /* SET_ADDRESS status stage completed */
        USBFADR = g_pending_addr;
        g_class_tag = 0;
    }
    __asm
        .globl _ep0_clear_stall_toggle_and_arm
        lcall _ep0_clear_stall_toggle_and_arm
    __endasm;
}
