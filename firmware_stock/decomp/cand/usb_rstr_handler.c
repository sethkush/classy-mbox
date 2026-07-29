// MATCH: image=rev20 addr=0x0F43 len=45 func=usb_rstr_handler cflags=--peep-file,firmware_stock/decomp/keil.peep

/* VECINT 0x17 (RSTR) -- the USB engine saw a bus reset. Table entry 0x17 at
 * 0x0CC1 reads 0F 43, which is the XREF Ghidra shows.
 *
 * This runs in interrupt context and puts the device back into the
 * default/unconfigured state that USB 2.0 9.1.1.2 requires after a reset:
 *
 *   - both audio streams stood down: IEPDCNTX1 (0xFF63, the capture endpoint
 *     EP1-IN) and OEPDCNTX2 (0xFF9B, the playback endpoint EP2-OUT) are
 *     zeroed, which clears their byte counts and their NAK bits;
 *   - USBFADR = 0, back to the default address;
 *   - IEPCNF0 = OEPCNF0 = 0x84 -- UBME (bit 7), so the UBM owns EP0 again,
 *     plus the per-endpoint interrupt enable (bit 2). Writing the whole byte
 *     also drops STALL and TOGGLE. Same constant TI's engUsbInit uses;
 *   - USBCTL |= 0xC0 -- CONN (bit 7) and FEN (bit 6). A bus reset clears FEN
 *     in hardware, so it has to be put back or the function core stays
 *     disabled; CONN is already set and the OR is harmless;
 *   - four state bits cleared: 0x0A (IRAM 0x21.2), 0x0E (0x21.6, configured),
 *     0x08 (0x21.0, interface 1 alt != 0) and 0x09 (0x21.1, interface 2
 *     alt != 0). These are BIT addresses, not the IRAM bytes 0x08..0x0E that
 *     hold the clock mode, the EP0 length counters and the deferred address;
 *   - USBIMSK = 0x9F, reached with a 1-byte INC DPTR off USBCTL.
 *
 * On the mask value: 0x9F is RSTR(7) | SOF(4) | PSOF(3) | SETUP(2) | bit 1 |
 * STPOW(0), and it deliberately leaves SUSR(6) and RESR(5) masked. This is
 * the same value the power-on path writes at 0x09EC..0x09F1. Suspend/resume
 * reporting is only turned on later, when a non-zero alternate setting is
 * selected: 0x03F1..0x03F6 writes USBIMSK = 0xFF. So a reset does not merely
 * restore the mask, it demotes it -- suspend detection is a streaming-time
 * feature here, and every bus reset switches it back off.
 *
 * WRITTEN AS ASSEMBLY. Two Keil habits that SDCC has no way to reproduce:
 *
 *   1. A is live across the LCALL at 0x0F43. ep0_arm_zlp_and_out ends with
 *      A == 0 (it got there via CLR A at 0x0B85), and Keil knew that, so the
 *      next four stores are bare MOVX @DPTR,A with no CLR A of their own.
 *      That is inter-procedural register analysis, the same thing that forced
 *      std_get_interface into cand/partial/ -- except here it happens at four
 *      sites, not one, so a single "partial=N at=one offset" cannot describe
 *      it honestly.
 *   2. A is then held again from `MOV A,#0x84` at 0x0F55 across the OEPCNF0
 *      store and reused for IEPCNF0, and DPTR is carried from USBCTL to
 *      USBIMSK with INC DPTR across four unrelated bit clears.
 *
 * That is the hw_master_init class exactly (decomp/README.md, "When to stop
 * using C"), and the body is straight-line register programming, so the
 * assembly is as readable as the C would have been.
 *
 * Rev 22 has this function at 0x0F64, byte-identical for all 45 bytes except
 * the LCALL operand: 0x0F64 is `12 0B 75`, calling Rev 22's copy of
 * ep0_arm_zlp_and_out at 0x0B75 rather than Rev 20's 0x0B82. Everything from
 * `90 ff 9b` onward matches byte for byte, USBIMSK = 0x9F included. */
void usb_rstr_handler(void) __naked {
    __asm
        .globl _ep0_arm_zlp_and_out

        lcall _ep0_arm_zlp_and_out ; both EP0 halves armed for a ZLP; returns A=0

        mov   dptr,#0xff9b         ; OEPDCNTX2 -- playback stream, host->device
        movx  @dptr,a              ; A still 0 from the callee
        mov   dptr,#0xff63         ; IEPDCNTX1 -- capture stream, device->host
        movx  @dptr,a
        mov   dptr,#0xffff         ; USBFADR
        movx  @dptr,a              ; back to the default address

        mov   dptr,#0xffa8         ; OEPCNF0
        mov   a,#0x84              ; UBME | interrupt enable; clears STALL/TOGGLE
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
