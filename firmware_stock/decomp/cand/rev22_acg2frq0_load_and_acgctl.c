// MATCH: image=rev22 addr=0x0EE8 len=11 func=acg2frq0_load_and_acgctl cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Store the caller's A into ACG2FRQ0 (0xFFF9) -- the last byte of a
 * synthesizer frequency word -- and then commit both generators by writing
 * ACGCTL = 0x06.
 *
 * A IS AN INPUT PARAMETER, in the accumulator, which no C51 calling convention
 * produces: Keil passes the first char in R7.  Hence __naked.  Two ways in:
 *
 *   - fallen into from acg_both_synths_24576khz (0x0EC8), which leaves
 *     A = 0x0F, completing the 48 kHz word 0x61A80F.
 *   - LCALLed from audio_clock_set_mode 0x0766 with A = 0x20, completing the
 *     44.1 kHz word 0x6A4B20.
 *
 * ACGCTL = 0x06 is the commit for both cases.  It also means this routine
 * RETURNS WITH DPTR = 0xFFE1, which mode 5 of audio_clock_set_mode relies on:
 * it follows the call with a bare `INC DPTR` to reach ACG1DCTL at 0xFFE2.
 *
 * ---------------------------------------------------------------------------
 * REV 20 -> REV 22 DELTA.  Rev 20's equivalent entry point is 0x0E0F, called
 * sfr_store_then_acg_ctl6 in symbols.map (Ghidra: acg_commit_and_ctl), and it
 * is 8 bytes, not 11:
 *
 *     rev20 0x0E0F:  MOVX @DPTR,A / MOV DPTR,#0xFFE1 / MOV A,#6 / MOVX / RET
 *     rev22 0x0EE8:  MOV DPTR,#0xFFF9 / MOVX @DPTR,A /
 *                    MOV DPTR,#0xFFE1 / MOV A,#6 / MOVX / RET
 *
 * i.e. Rev 22 absorbed the `MOV DPTR,#0xFFF9` that Rev 20 required each caller
 * to perform.  Behaviour is identical -- every Rev 20 caller loaded exactly
 * that value -- but the calling contract changed: Rev 20's entry takes both A
 * and DPTR live, Rev 22's takes only A.  Because the containing 43-byte block
 * is the same size in both images, the byte paid for here is recovered by the
 * transposition described in rev22_acg_both_synths_24576khz.c, and the callers
 * keep the 3 bytes each.
 *
 * The same "extracted block grew a DPTR load" shift shows up independently at
 * codec_port_cfg3_commit (rev20 0x0FF4, 13 B) -> cport_cnf3_write_enable
 * (rev22 0x0FE2, 16 B).
 */
void acg2frq0_load_and_acgctl(void) __naked {
    __asm
        mov   dptr,#0xfff9         ; ACG2FRQ0
        movx  @dptr,a              ; caller's A: 0x0F for 48 kHz, 0x20 for 44.1
        mov   dptr,#0xffe1         ; ACGCTL
        mov   a,#0x06
        movx  @dptr,a              ; commit: load both synthesizers from FRQ
        ret                        ; returns with DPTR = 0xFFE1
    __endasm;
}
