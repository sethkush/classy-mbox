// MATCH: image=rev22 addr=0x0F64 len=45 func=usb_rstr_handler cflags=--peep-file,firmware_stock/decomp/keil.peep

/* VECINT 0x17 (RSTR_INT) -- the USB engine saw a bus reset. Rev 22 at 0x0F64.
 * Reached from the vector-address table through usb_isr_int0_vecdispatch: the
 * Rev 22 table is at 0x0C7D and its entry 0x17 sits at 0x0C7D + 2*0x17 =
 * 0x0CAB, reading 0F 64 (rev20: table 0x0C93, entry at 0x0CC1, reading 0F 43).
 * The VECINT constant is TI's, from
 * reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h (RSTR_INT 0x17).
 *
 * REV 20 -> REV 22 DELTA: ONE BYTE, AND IT IS A RELOCATION.
 * rev20 0x0F43 and rev22 0x0F64 are the same 45 bytes except the operand of
 * the opening LCALL: rev22 `12 0B 75` against rev20 `12 0B 82`. Both call the
 * same routine -- "zero OEPDCNTX0 and IEPDCNTX0, leaving A == 0" -- which
 * Rev 20 places at 0x0B82 and Rev 22 at 0x0B75 (Ghidra names it ep0_flush_arm
 * in the Rev 22 listing, ep0_arm_zlp_and_out in the Rev 20 one; the bodies are
 * the same five instructions). Every byte from `90 ff 9b` onward is identical,
 * USBIMSK = 0x9F included. NO BEHAVIOURAL CHANGE. The Rev 20 candidate
 * cand/usb_rstr_handler.c ported with one operand edited.
 *
 * WHAT IT DOES. This runs in interrupt context and puts the device back into
 * the default/unconfigured state USB 2.0 9.1.1.2 requires after a reset:
 *
 *   - both audio streams stood down: OEPDCNTX2 (0xFF9B, the playback endpoint
 *     EP2-OUT) and IEPDCNTX1 (0xFF63, the capture endpoint EP1-IN) are zeroed,
 *     which clears their data counts and their NACK bits (bit 7 of each,
 *     datasheet 6.4.4.1 and the OUT equivalent);
 *   - USBFADR = 0, back to the default address;
 *   - OEPCNF0 = IEPCNF0 = 0x84 -- endpoint enable (bit 7) plus the
 *     per-endpoint interrupt enable (bit 2), i.e. the UBM owns EP0 again.
 *     Writing the whole byte also drops STALL (bit 3) and TOGGLE (bit 5).
 *     Same constant TI's engUsbInit uses, and the same one
 *     rev22_usb_ep_dma_init writes at power-on;
 *   - USBCTL |= 0xC0 -- CONN (bit 7) and FEN (bit 6). A bus reset clears FEN
 *     in hardware, so it has to be put back or the function core stays
 *     disabled; CONN is already set and the OR is harmless;
 *   - four state bits cleared: 0x0A (IRAM 0x21.2), 0x0E (0x21.6, configured),
 *     0x08 (0x21.0, interface 1 alt != 0) and 0x09 (0x21.1, interface 2 alt
 *     != 0). These are BIT addresses. The IRAM BYTES 0x08..0x0E, which hold
 *     the EP0 transfer-length counters, the class tag and the deferred USB
 *     address, are untouched here;
 *   - USBIMSK = 0x9F, reached with a 1-byte INC DPTR off USBCTL.
 *
 * ON THE MASK VALUE, and this is the part that matters for Rev 22. 0x9F is
 * RSTR(7) | SOF(4) | PSOF(3) | SETUP(2) | bit 1 | STPOW(0), and it leaves
 * SUSR(6) and RESR(5) masked. Bit 4 -- SOF -- is set, in BOTH images. So the
 * SOF interrupt was already unmasked in Rev 20 and was already reaching the
 * dispatcher; Rev 20 simply routed it to a one-byte RET stub at 0x1034. The
 * Rev 22 fix is therefore exactly two things: this mask is unchanged, one
 * vector-table word changed (0x0CA5 now reads 0D 58), and a 70-byte handler
 * was added at 0x0D58. Nothing in the reset path had to be touched.
 *
 * Suspend/resume reporting stays a streaming-time feature: it is only turned
 * on when a non-zero alternate setting is selected (rev22 0x0349..0x034E
 * writes USBIMSK = 0xFF; rev20 0x03F1..0x03F6), so every bus reset demotes the
 * mask back to 0x9F.
 *
 * WRITTEN AS ASSEMBLY, for the same two Keil habits as in Rev 20:
 *
 *   1. A is live across the opening LCALL. The callee at 0x0B75 ends with
 *      A == 0 (it got there via CLR A at 0x0B78), and Keil knew that, so the
 *      next three stores are bare MOVX @DPTR,A with no CLR A of their own.
 *      That is inter-procedural register analysis -- the same capability that
 *      forced std_get_interface into cand/partial/ -- except here it happens
 *      at three sites, so one "partial=N at=<one offset>" could not describe
 *      it honestly.
 *   2. A is then held again from `MOV A,#0x84` across the OEPCNF0 store and
 *      reused for IEPCNF0, and DPTR is carried from USBCTL to USBIMSK with a
 *      1-byte INC DPTR across four unrelated bit clears.
 *
 * That is the hw_master_init class exactly (decomp/README.md, "When to stop
 * using C"), and the body is straight-line register programming, so the
 * assembly carries the meaning as well as the C would. */
void usb_rstr_handler(void) __naked {
    __asm
        .globl _ep0_flush_arm

        lcall _ep0_flush_arm       ; rev22 0x0B75 (rev20 0x0B82) -- zeroes both
                                   ; EP0 data counts and returns with A == 0

        mov   dptr,#0xff9b         ; OEPDCNTX2 -- playback stream, host->device
        movx  @dptr,a              ; A still 0 from the callee
        mov   dptr,#0xff63         ; IEPDCNTX1 -- capture stream, device->host
        movx  @dptr,a
        mov   dptr,#0xffff         ; USBFADR
        movx  @dptr,a              ; back to the default address

        mov   dptr,#0xffa8         ; OEPCNF0
        mov   a,#0x84              ; OEPEN | OEPIE; clears STALL and TOGGLE
        movx  @dptr,a
        mov   dptr,#0xff68         ; IEPCNF0
        movx  @dptr,a              ; A still 0x84

        mov   dptr,#0xfffc         ; USBCTL
        movx  a,@dptr
        orl   a,#0xc0              ; CONN | FEN -- the reset cleared FEN
        movx  @dptr,a

        clr   0x0a                 ; IRAM 0x21.2
        clr   0x0e                 ; IRAM 0x21.6 -- no longer configured
        clr   0x08                 ; IRAM 0x21.0 -- interface 1 back to alt 0
        clr   0x09                 ; IRAM 0x21.1 -- interface 2 back to alt 0

        inc   dptr                 ; 0xFFFC -> 0xFFFD, USBIMSK, DPTR still live
        mov   a,#0x9f              ; RSTR|SOF|PSOF|SETUP|STPOW; SUSR/RESR off
        movx  @dptr,a
        ret
    __endasm;
}
