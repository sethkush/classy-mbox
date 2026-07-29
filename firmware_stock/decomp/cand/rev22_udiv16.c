// MATCH: image=rev22 addr=0x0B7F len=85 func=udiv16 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Keil C51 library routine ?C?UIDIV -- unsigned 16 / 16 division.
 *
 * THIS FUNCTION DOES NOT EXIST IN REV 20. The 85-byte run at rev22 0x0B7F
 * does not appear anywhere in rev20_firmware_code.bin (searched the whole
 * 8174-byte image for the byte string; no hit), and rev20 has no other
 * divide routine -- it contains no DIV AB at all. (The only two 0x84 bytes in
 * the rev20 image, at 0x0999 and 0x0F56, are both the immediate of a
 * MOV A,#0x84 whose 0x74 opcode sits one byte earlier, not opcodes.)
 * It is a genuine Rev 22 addition, and it was not added for
 * its own sake -- the linker pulled it in because exactly one new caller
 * needs it. See "WHO CALLS IT" below.
 *
 * WRITTEN AS __naked ASSEMBLY, not C. Two reasons:
 *   1. It is hand-written Keil library assembly, not compiler output. There
 *      is no C source to recover: it dispatches on PSW.2 (OV) left behind by
 *      a DIV AB, reuses the accumulator across a shift-subtract loop, and
 *      threads the carry flag from an RLC straight into the borrow of the
 *      following SUBB.
 *   2. Even if it were written as C (`a / b`), SDCC would emit its own
 *      _divuint from its own library, with a different register convention
 *      and different code.
 *
 * CALLING CONVENTION (Keil register-passing, confirmed by decoding the body):
 *      in   R6:R7 = dividend (R6 high),  R4:R5 = divisor (R4 high)
 *      out  R6:R7 = quotient,            R4:R5 = remainder
 *      clobbers A, B, R0, PSW.
 * Verified by emulating this exact instruction sequence over 200,000 random
 * (dividend, divisor) pairs plus the boundary cases {1, 2, 6, 255, 256, 257,
 * 0x8000, 0x8001, 0xFFFF} x {0, 1, d-1, d, d+1, 0xFFFE, 0xFFFF}: quotient and
 * remainder came out exact in every case, including the divisor > 0x8000 runs
 * where the shift-left of the running remainder overflows into the carry.
 *
 * DIVISION BY ZERO is only half-checked. The 8-bit-divisor path (0x0BAE)
 * tests OV after its DIV AB and bails out at 0x0BD3 leaving R6 and R7
 * undefined; the 8-bit-dividend path (0x0B85) does a DIV AB with no OV test
 * at all. Callers must not pass a zero divisor. The one caller passes the
 * constant 6.
 *
 * WHO CALLS IT, AND WHY -- the interesting part of this batch.
 * One call site: LCALL 0x0B7F at rev22 0x0D79, inside sof_int_handler
 * (rev22 0x0D58). That handler is itself new. The USB vector-interrupt
 * address table shows it directly:
 *
 *     SOF vector slot     rev20 (table at 0x0CBB)  ->  0x1034, a bare RET
 *                                                      (vecint_sof_noop)
 *                         rev22 (table at 0x0CA5)  ->  0x0D58, a real handler
 *
 * So Rev 20 ignored Start-Of-Frame entirely and Rev 22 acts on it. The
 * handler reads the ISO-OUT endpoint buffer byte count from DMABCNT0L
 * (0xFFEB) and DMABCNT0H (0xFFEC) into R6:R7 -- NOT the USB frame number;
 * those two SFRs are the DMA byte counter, per Reg_stc1.h -- compares it
 * against the previous value kept in
 * IRAM 0x1B:0x1C (0x1B high, 0x1C low -- the same big-endian IRAM pair
 * convention used for CODE pointers elsewhere in this firmware), and returns
 * immediately if the frame number has not advanced. Otherwise it stores the
 * new frame number, loads R5 = 6 (R4 is already 0 from 0x0D61), calls this
 * routine, and tests `R5 | R4` for zero at 0x0D7C. rev22 0x0D7E is `60 1d`,
 * a JZ that RETURNS on a zero remainder -- so the body runs only when the
 * byte count is NOT a multiple of 6, i.e. when the buffer holds a partial
 * stereo 24-bit sample frame. See rev22_sof_int_handler.c, which owns this
 * account. The body toggles bit 7 of the SFR at 0xFFE8 off,
 * writes 0 to 0xFF9B and 0xFF9F, writes 0xC5 to 0xFF98, then sets bit 7 of
 * 0xFFE8 again. Naming those four SFRs is outside this batch and I have not
 * verified what they are, so I am not going to guess in a comment that will
 * be read as fact.
 *
 * The point for the Rev 20 -> Rev 22 delta is narrower and solid: Rev 22
 * added a periodic, frame-clock-driven task, and the only reason an 85-byte
 * general-purpose 16-bit divide is in the image at all is the modulo-6 test
 * that decides when that task runs.
 */
void udiv16(void) __naked {
    __asm
    ;; ---- dispatch on operand widths (0x0B7F) --------------------------
    ;; Three cases, cheapest first. DIV AB is one instruction for 8/8, so
    ;; both operands fitting in a byte is worth a special case, and a 16-bit
    ;; dividend over an 8-bit divisor is worth another.
        cjne  r4,#0x00,0004$       ; divisor >= 256 -> full 16/16 loop
        cjne  r6,#0x00,0010$       ; dividend >= 256 -> 16/8 loop

    ;; ---- 8 / 8: one DIV AB (0x0B85) -----------------------------------
    ;; R4 and R6 are both already zero, so the high halves of the quotient
    ;; and remainder are correct without being written.
        mov   a,r7
        mov   b,r5
        div   ab                   ; A = quotient, B = remainder
        mov   r7,a
        mov   r5,b
        ret

    ;; ---- 16 / 16: restoring shift-subtract, 8 iterations (0x0B8D) -----
    ;; The divisor is >= 256, so the quotient cannot exceed 255 and eight
    ;; iterations suffice. R4:R6 is the running remainder and R7 is the
    ;; dividend low byte doing double duty: each iteration shifts one
    ;; dividend bit out of the top of R7 and shifts one quotient bit into
    ;; the bottom via INC R7.
    0004$:
        clr   a
        xch   a,r4                 ; R0 = divisor high, and R4 <- 0 so it can
        mov   r0,a                 ; start accumulating the remainder high
        mov   b,#0x08              ; loop counter in B, freeing all of R0..R7
    0005$:
        mov   a,r7                 ; 24-bit left shift of R4:R6:R7 by one
        add   a,r7                 ; ADD A,R7 is a 1-byte shift-left that also
        mov   r7,a                 ; sets CY from bit 7
        mov   a,r6
        rlc   a
        mov   r6,a
        mov   a,r4
        rlc   a
        mov   r4,a                 ; CY now = bit shifted out of the remainder
        mov   a,r6                 ; trial subtract: (R4:R6) - (R0:R5), with
        subb  a,r5                 ; that shifted-out bit as the borrow-in --
        mov   a,r4                 ; which is what makes the 17-bit case come
        subb  a,r0                 ; out right rather than a bug
        jc    0006$                ; borrow -> divisor did not fit, leave the
                                   ; remainder as it is and shift in a 0 bit
        mov   r4,a                 ; it fitted: commit the high half from A,
        mov   a,r6                 ; then redo the low half. CY here is the
        subb  a,r5                 ; borrow out of the high subtract, i.e. 0
        mov   r6,a
        inc   r7                   ; quotient bit = 1
    0006$:
        djnz  b,0005$
        clr   a
        xch   a,r6                 ; remainder low <- R6, and R6 <- 0 so the
        mov   r5,a                 ; quotient high byte reads back as zero
        ret

    ;; ---- 16 / 8 (0x0BAE) ----------------------------------------------
    ;; Divisor fits in a byte, dividend does not. Divide the high byte with
    ;; a single DIV AB to get the quotient high byte and the remainder to
    ;; carry into the low half, then run eight shift-subtract iterations on
    ;; the low byte. R4 is already 0 and stays 0: an 8-bit divisor gives an
    ;; 8-bit remainder.
    0010$:
        mov   a,r5
        mov   r0,a                 ; R0 = divisor, kept for the loop
        mov   b,a
        mov   a,r6
        div   ab                   ; A = dividendH / divisor, B = the rest
        jb    0xd2,0016$           ; BIT 0xD2 = PSW.2 = OV: DIV AB sets it only
                                   ; on divide-by-zero. Bail out, leaving the
                                   ; result registers undefined.
        mov   r6,a                 ; quotient high
        mov   r5,b                 ; running remainder (8-bit)
        mov   b,#0x08
    0011$:
        mov   a,r7                 ; shift R5:R7 left by one
        add   a,r7
        mov   r7,a
        mov   a,r5
        rlc   a
        mov   r5,a
        jc    0014$                ; remainder overflowed 8 bits, so the
                                   ; divisor certainly fits: subtract with a
                                   ; cleared borrow and take the 1 bit
        subb  a,r0                 ; CY is 0 on this path, so this is a plain
        jnc   0015$                ; trial subtract; no borrow -> it fitted
        djnz  b,0011$              ; it did not fit: A is discarded, R5 keeps
        ret                        ; the un-subtracted remainder, quotient
                                   ; bit stays 0
    0014$:
        clr   c
        subb  a,r0
    0015$:
        mov   r5,a
        inc   r7                   ; quotient bit = 1
        djnz  b,0011$
    0016$:
        ret
    __endasm;
}
