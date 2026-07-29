// MATCH: image=rev22 addr=0x0EFC len=53 func=shiftreg_out8_p1hi cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Front-panel shift-register chain A: clock 8 bits out on the high nibble of
 * P1, then strobe.  Rev 22 counterpart of rev20 shiftreg8_commit (0x0F0C).
 * Eight call sites in rev22 (0x03A6, 0x03EC, 0x0461, 0x0470, 0x0503, 0x0864,
 * 0x0885, 0x0A90).
 *
 * Pin assignment, from the read-modify-writes below -- the top nibble of P1,
 * mirroring what shiftreg_out16_p1 (0x0E56) does with the bottom nibble:
 *      P1.7  serial data   (ORL 0x90,#0x80 / ANL 0x90,#0x7F)
 *      P1.5  shift clock   (ORL 0x90,#0x20 then ANL 0x90,#0xDF)
 *      P1.6  strobe        (cleared on entry at 0x0F00, driven again at the end)
 *
 * Payload: IRAM 0x22, MSB first.  That byte is the one the two source-selector
 * state machines write -- panel_state_cycle_A (0x0E1B) owns bits 2:0 and
 * panel_state_cycle_B (0x0E8F) owns bits 5:3 -- so this is the routine that
 * puts the Mic/Line/Inst selection onto hardware, ring 0b101 Mic -> 0b011 Line
 * -> 0b110 Inst.
 *
 * IRAM 0x22.6 = bit address 0x16 is the derived panel bit.  The full account
 * of that one bit and its several writers is in cand/shiftreg8_commit.c, which
 * owns it; it holds for Rev 22 with the addresses moved.  Summarising only:
 * the bit is write-only (rev22 SETB at 0x045C, 0x0E49, 0x0EBA; CLR at 0x046B,
 * 0x0501, 0x0E4E, 0x0E53, 0x0EBF, 0x0EC4; no JB/JNB/JBC/CPL/MOV C anywhere),
 * IRAM 0x22 is loaded only here (0x0EFE `MOV R7,0x22`), its nominal value is
 * !(f_spdif | f_force) recomputed in both state machines' tails, and the two
 * command handlers plus the EEPROM self-test write the derived value directly
 * instead of recomputing it.  So it is bit 6 of the chain-A latch byte, one
 * output line, not three separate flags.
 *
 * THE TAIL IS ASYMMETRIC, and that is stock.  With bit 0x1E clear it does the
 * expected thing: drop the data line, pulse P1.6 high then low, return.  With
 * bit 0x1E set (0x0F20 `JNB 0x1E,0x0F27`) it instead drives P1.7 and P1.6 both
 * high with a single ORL 0x90,#0xC0 and returns, leaving them there.
 *
 * Bit 0x1E is IRAM 0x23 bit 6, toggled by toggle_bit1E_state (0x1020) when the
 * P3.5 front-panel button acts (see p3_edge_poll_dispatch, 0x0F31).  What the
 * pin pair does physically in that held-high state is NOT established from the
 * firmware -- only the pin behaviour is.  An older note in this project called
 * 0x23.6 "48 V phantom power"; nothing in either image supports that and it is
 * withdrawn.  Note also that IRAM 0x23 is chain B's first payload byte, so its
 * bit 6 is simultaneously shifted out by shiftreg_out16_p1; the two uses share
 * the same storage.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY: the loop body is Keil's inline expansion of
 * the <intrins.h> intrinsic `_crol_(x, 1)`, a DJNZ countdown around a single
 * RL A.  SDCC emits one RL A for the equivalent C and no peephole rule short of
 * expanding one instruction into six specific ones would close it.
 *
 * REV 20 -> REV 22 DELTA: two bytes, same register-allocation change as chain
 * B.  Rev 20 kept the working byte in R5 and copied it to R7 for the intrinsic
 * (`MOV R5,0x22` at entry, `MOV R7,0x05` and `MOV R5,A` in the loop); Rev 22
 * loads it straight into R7 (0x0EFE `MOV R7,0x22`, `MOV R7,A` in the loop) and
 * drops the two-byte copy.  55 bytes -> 53, and the loop's DJNZ displacement
 * shifts by two (rev20 de e1, rev22 de e3).  Every P1 mask, the payload
 * address, the 0x1E test and the whole asymmetric tail are byte-identical.
 */
void shiftreg_out8_p1hi(void) __naked {
    __asm
        mov   r6,#0x08             ; 8 bits
        mov   r7,0x22              ; panel source/derived-bit byte
        anl   0x90,#0xbf           ; P1.6 = 0 before shifting

    bitloop$:
        ; --- _crol_(r7, 1): old bit 7 ends up in bit 0 ----------------------
        mov   r0,#0x01             ; rotate count
        mov   a,r7                 ; working byte lives in R7 (rev22)
        inc   r0
        sjmp  rotest$
    rotate$:
        rl    a
    rotest$:
        djnz  r0,rotate$
        mov   r7,a

        ; --- present that bit on P1.7, clock it in on P1.5 ------------------
        jnb   0xe0,data0$          ; ACC.0
        orl   0x90,#0x80           ; P1.7 = 1
        sjmp  clock$
    data0$:
        anl   0x90,#0x7f           ; P1.7 = 0
    clock$:
        orl   0x90,#0x20           ; P1.5 high -- data sampled on this edge
        anl   0x90,#0xdf           ; P1.5 low
        djnz  r6,bitloop$

        jnb   0x1e,strobe$         ; IRAM 0x23.6, the P3.5 button toggle
        orl   0x90,#0xc0           ; hold P1.7 and P1.6 high, and leave them
        ret
    strobe$:
        anl   0x90,#0x7f           ; P1.7 = 0
        orl   0x90,#0x40           ; P1.6 high
        anl   0x90,#0xbf           ; P1.6 low  -- one clean strobe
        ret
    __endasm;
}
