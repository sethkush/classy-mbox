// MATCH: image=rev22 addr=0x0575 len=8 func=cs8427_write_shadowed cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Emit the (register, value) pair the CALLER has already staged in IRAM bytes
 * 0x2C:0x2D to the external audio chip. Rev 22 at 0x0575, 8 bytes.
 *
 * Three instructions and a RET: reload Keil's register parameters from the
 * staging pair (R7 = parameter 1 = register index, R5 = parameter 2 = value)
 * and call spi3wire_write_3bytes at 0x0C31. Ghidra's "shadowed" name refers to
 * 0x2C/0x2D being a RAM shadow of the write-only chip registers; that reading
 * is plausible but I have not verified anything ever reads the pair back, so
 * treat the name as a label, not a claim.
 *
 * ENTERING WITH THE ARGUMENTS ALREADY SET IS THE TELL that this was never a
 * source-level function -- a real function would take parameters. It is a
 * Keil common-block extraction: the compiler found the same three-instruction
 * argument-reload-and-call in several places and pulled it out.
 *
 * The two call sites are the ANALOGUE branch of the two clock-mode handlers,
 * and each stages the pair itself immediately before calling:
 *
 *     rev22 0x048E-0x0494  0x2C = 0x23, 0x2D = 0x00  -> reg 0x23 = 0x00
 *     rev22 0x04AF-0x04B5  0x2C = 0x23, 0x2D = 0x40  -> reg 0x23 = 0x40
 *
 * Those sit on the `JNB 0x2C` analogue path (BIT 0x2C = IRAM 0x25.4, S/PDIF
 * selected) at 0x0482 and 0x04A4; the S/PDIF path calls
 * cs8427_write_reg04_val41 (0x0567) instead. So bit 0x40 of chip register
 * 0x23 is the single thing that differs between the two analogue modes. What
 * it selects is NOT established -- no CS8427 datasheet exists under
 * reference/, and 0x23 is outside the register range that part is believed to
 * document, which is one of the reasons the chip identification is still
 * rated only "likely" (firmware_stock/disasm/rev20_ANNOTATED.md:270).
 *
 * ===================== REV 20 -> REV 22 DELTA ==========================
 *
 * SAME THREE INSTRUCTIONS, SMALLER BLOCK, IDENTICAL WIRE SEQUENCE. Rev 20's
 * counterpart block at 0x0582 (serial_ctl_write_caller_pair_then_24_80,
 * 20 bytes) did this write AND THEN a second one, reg 0x24 = 0x80, staging
 * 0x2C/0x2D itself and tail-calling with an LJMP. Rev 22's block stops after
 * the first write and returns.
 *
 * The reg 0x24 = 0x80 write is not lost -- it moved out into the callers,
 * traced and verified: after `LCALL 0x0575` each caller does `MOV 0x2C,#0x24
 * / MOV 0x2D,#0x80` (rev22 0x0497 and 0x04B8) and then reaches the shared
 * write tail at 0x0509, `MOV R5,0x2D / MOV R7,0x2C / LCALL 0x0C31`. Net wire
 * sequence, reg 0x23 = <mode byte> then reg 0x24 = 0x80, is exactly Rev 20's.
 *
 * A boundary that moves between revisions while the emitted sequence does not
 * is the signature of an optimiser artefact, not a design change -- the same
 * argument that applies to Rev 20's four extchip_* helpers, which Rev 22 does
 * not have at all.
 *
 * The call target is pure relocation: rev20 0x0C45 -> rev22 0x0C31.
 *
 * __naked and all-assembly: there is no C here to write. Every instruction is
 * either an R7/R5 load in Keil's register-parameter convention, which SDCC
 * cannot express, or the call itself.
 */
void cs8427_write_shadowed(void) __naked {
    __asm
        .globl _spi3wire_write_3bytes
        mov   r5,0x2d              ; R5 = val  (BYTE 0x2D, staged by caller)
        mov   r7,0x2c              ; R7 = reg  (BYTE 0x2C, not bit 0x2C)
        lcall _spi3wire_write_3bytes ; 0x0C31: writes {0x20, reg, val}
        ret
    __endasm;
}
