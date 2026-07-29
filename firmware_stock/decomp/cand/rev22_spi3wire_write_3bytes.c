// MATCH: image=rev22 addr=0x0C31 len=76 func=spi3wire_write_3bytes cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Bit-banged 3-byte serial control write to the external audio chip, Rev 22
 * at 0x0C31. Counterpart of Rev 20's cs8427_ctl_write at 0x0C45 (78 bytes).
 *
 *      spi3wire_write_3bytes(reg, val)     reg in R7, val in R5
 *
 * Keil's register calling convention puts the first char parameter in R7 and
 * the second in R5. Both Rev 22 wrappers confirm it independently:
 * cs8427_write_reg04_val41 (0x0567) and cs8427_write_shadowed (0x0575) stage
 * the pair in IRAM 0x2C/0x2D and reload `MOV R5,0x2D / MOV R7,0x2C` in that
 * order immediately before the call, and audio_hw_bringup (0x09B6) loads the
 * literals straight into R7/R5 at its ten call sites.
 *
 * Wire format, three bytes, MSB first, on P1.4 = data and P1.3 = clock
 * (clock pulsed high then low after each bit):
 *      0x20      fixed leading byte (rev22 0x0C35, rev20 0x0C4B)
 *      reg       register index
 *      val       value
 *
 * Chip select is bit 0x2F -- IRAM 0x25 bit 7 -- which is NOT a port pin. It
 * is the top bit of the high payload byte of the 16-bit shift-register chain,
 * so asserting it means clearing the bit (0x0C39) and calling
 * shiftreg_out16_p1 (0x0E56), and releasing it means setting it and calling
 * again (0x0C77/0x0C79). Every transaction is therefore framed by two full
 * 16-bit panel shifts, and each one rewrites the panel latch outputs with
 * their current values as a side effect.
 *
 * ===================== REV 20 -> REV 22 DELTA ==========================
 *
 * IDENTICAL ON THE WIRE, TWO BYTES SHORTER, from one register-allocation
 * change at the prologue. Verified instruction by instruction; the only
 * differing bytes are these:
 *
 *     rev20 0x0C45  MOV 0x33,R7    (spill reg to IRAM 0x33)     3 B
 *     rev20 0x0C47  MOV R1,0x05    (copy val from R5 into R1)   2 B
 *     rev22 0x0C31  MOV R1,0x07    (copy reg from R7 into R1)   2 B
 *
 * and correspondingly where each byte of the frame is sourced:
 *
 *     byte 2 (register)  rev20 `MOV R3,0x33` (IRAM 0x33)   rev22 `MOV R3,0x01` (R1)
 *     byte 3 (value)     rev20 `MOV R3,0x01` (R1)          rev22 `MOV R3,0x05` (R5)
 *
 * Rev 20 saved the VALUE into R1 and spilled the REGISTER to memory; Rev 22
 * saves the REGISTER into R1 and leaves the VALUE in R5 untouched for the
 * whole routine. Only R7 needs saving at all, because the inline `_crol_`
 * expansion in the bit loop uses R7 as its parameter register (0x0C43
 * `MOV R7,0x03`); R5 is never touched, so Rev 22's choice is simply the
 * better one and costs no memory. Everything else is byte-for-byte the same:
 * the 0x20 lead byte, R4 = 8 bit counter, R2 = 1/2/3 byte counter, the
 * P1.4/P1.3 masks 0x10/0xEF/0x08/0xF7, and the two chip-select shifts.
 *
 * Note what did NOT change: IRAM 0x33 stops being used here, and 0x33 is the
 * byte immediately below the initial stack pointer (SP is set to 0x32 by the
 * C51 startup so the first push lands at 0x33). Rev 20's spill slot sat
 * directly under the first pushed return address. Rev 22 no longer touches
 * it. I have NOT checked whether anything else in Rev 22 writes IRAM 0x33, so
 * I make no claim that this fixed a collision -- only that the use is gone.
 *
 * Chip identity: ESTABLISHED -- the part on the far end of this bus is a
 * Cirrus Logic CS8427. See firmware_stock/decomp/FINDING_cs8427_confirmed.md.
 * The lead byte this routine emits is the evidence's first half: ALSA's CS8427
 * header gives CS8427_BASE_ADDR = 0x10, and an address byte for a write to
 * slave 0x10 is (0x10 << 1) | 0 = 0x20 -- the exact constant loaded here
 * (rev22 0x0C35 `7B 20` MOV R3,#0x20; rev20 0x0C4B, the same two bytes). The
 * second half is that every register carried over this bus decodes to a
 * coherent CS8427 field: audio_hw_bringup's ten writes at 0x09F8..0x0A3D
 * (rev20 0x0855..0x08A4) are CLOCKSOURCE, UDATABUF, CONTROL1, CONTROL2,
 * DATAFLOW, SERIALINPUT, SERIALOUTPUT and RECVERRMASK, with values an S/PDIF
 * transceiver in this product needs.
 *
 * This function's own name still describes the bus shape rather than the part,
 * which is the right name for it -- the wire format is three bytes MSB first
 * and knows nothing about CS8427 semantics. Register names elsewhere in this
 * decompilation are ALSA's (reference/cs8427/alsa_cs8427.h), a secondary
 * source: quote it as "ALSA's CS8427 header names this ...", not as datasheet
 * text.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY, for two compounding reasons: the R7/R5
 * register parameters, and the `_crol_(x,1)` intrinsic expansion -- a DJNZ
 * countdown around one RL A with the operand staged through R7 -- which is
 * the same construct that makes the shift-register commit routines assembly.
 */
void spi3wire_write_3bytes(void) __naked {
    __asm
        .globl _shiftreg_out16_p1

        mov   r1,0x07              ; R1 <- R7: save reg (parameter 1); R7 is
                                   ; about to be clobbered by the rotate below.
                                   ; val stays in R5 for the whole routine.
        mov   r4,#0x08             ; 8 bits left in the current byte
        mov   r3,#0x20             ; byte 1: the fixed chip-address byte
        mov   r2,#0x01             ; byte counter: 1 -> 2 -> 3
        clr   0x2f                 ; IRAM 0x25.7: chip select asserted low...
        lcall _shiftreg_out16_p1   ; ...and pushed out through the panel chain

    bitloop$:
        mov   a,r4
        jz    nextbyte$            ; this byte finished

        ; --- _crol_(r3, 1): old bit 7 rotates into bit 0 --------------------
        mov   r0,#0x01
        mov   r7,0x03              ; R7 <- R3 (the intrinsic's parameter reg)
        mov   a,r7
        inc   r0
        sjmp  rotest$
    rotate$:
        rl    a
    rotest$:
        djnz  r0,rotate$
        mov   r3,a

        ; --- present the bit on P1.4, clock it in on P1.3 -------------------
        jnb   0xe0,data0$          ; ACC.0
        orl   0x90,#0x10           ; P1.4 = 1
        sjmp  clock$
    data0$:
        anl   0x90,#0xef           ; P1.4 = 0
    clock$:
        orl   0x90,#0x08           ; P1.3 high
        anl   0x90,#0xf7           ; P1.3 low
        dec   r4
        sjmp  bitloop$

    nextbyte$:
        cjne  r2,#0x01,byte3$
        mov   r2,#0x02
        mov   r3,0x01              ; byte 2: R1 = the register index
        mov   r4,#0x08
        sjmp  bitloop$
    byte3$:
        cjne  r2,#0x02,done$
        mov   r2,#0x03
        mov   r3,0x05              ; byte 3: R5 = the value, never spilled
        mov   r4,#0x08
        sjmp  bitloop$

    done$:
        setb  0x2f                 ; chip select released...
        lcall _shiftreg_out16_p1   ; ...and pushed out through the panel chain
        ret
    __endasm;
}
