// MATCH: image=rev22 addr=0x0103 len=8 func=ep0_arm_in_3bytes cflags=--peep-file,firmware_stock/decomp/keil.peep

/* Arm a three-byte EP0 IN reply.  The only caller is the class GET_CUR
 * SAMPLING_FREQ arm inside usb_setup_handler, which reaches it with the
 * two-byte SJMP at rev22 0x00FE after writing three bytes into the EP0 IN
 * buffer.  A short jump only reaches an adjacent target, which is why this
 * sits at 0x0103, immediately past the end of usb_setup_handler and past the
 * 3-byte stall trampoline at 0x0100.
 *
 * REV 20 -> REV 22: Rev 20's equivalent is send_3byte_ep0_reply at 0x010D --
 * also immediately after the same handler, also reached by SJMP from all three
 * of its success paths -- but there it was 11 bytes because it open-coded the
 * MOVX and both stage flags (cand/setup_get_sample_freq.c models it as the
 * tail of that function for exactly this reason).  Rev 22 is 8 bytes because
 * the last four operations moved into the shared ep0_arm_in_and_done at
 * 0x0247.  Same behaviour: IEPDCNTX0 = 3, no OUT stage, IN stage armed.
 *
 * WRITTEN AS ASSEMBLY because it ends by handing DPTR and A to the callee and
 * therefore has no RET of its own.
 */
void ep0_arm_in_3bytes(void) __naked {
    __asm
        .globl _ep0_arm_in_and_done   ; rev22 0x0247
        mov   dptr,#0xff6b         ; IEPDCNTX0
        mov   a,#0x03              ; three bytes: the 24-bit sample rate
        ljmp  _ep0_arm_in_and_done
    __endasm;
}
