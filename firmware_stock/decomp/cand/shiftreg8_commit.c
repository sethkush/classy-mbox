// MATCH: image=rev20 addr=0x0F0C len=55 func=shiftreg8_commit cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Front-panel shift-register chain A: clock 8 bits out on P1, then strobe.
 *
 * Ghidra calls this shiftreg8_commit_p1_7_6_5; the C name is the one the rest
 * of the decompilation uses (firmware_stock/decomp/symbols.map).
 *
 * Pin assignment, from the read-modify-writes below -- the top nibble of P1,
 * mirroring what shiftreg16_commit (0x0E62) does with the bottom nibble:
 *      P1.7  serial data   (ORL 0x90,#0x80 / ANL 0x90,#0x7F)
 *      P1.5  shift clock   (ORL 0x90,#0x20 then ANL 0x90,#0xDF)
 *      P1.6  strobe        (cleared on entry, driven again at the end)
 *
 * Payload: IRAM 0x22 (g_mux_byte), MSB first. That byte is the one the two
 * source-selector state machines write -- button_a_cycle_3state (0x0E27) owns
 * bits 2:0 and button_b_cycle_3state (0x0E9D) owns bits 5:3. So this is the
 * routine that puts the Mic/Line/Inst selection onto hardware, and the ring is
 * 0b101 Mic -> 0b011 Line -> 0b110 Inst.
 *
 * IRAM 0x22.6 = BIT ADDRESS 0x16 -- ONE BIT, THREE WRITERS. THIS FILE IS THE
 * OWNER OF THAT ACCOUNT; cmd4_variantA_reapply_mode.c, cmd5_variantB_set_mode1.c,
 * cmd11_eeprom_selftest.c and i2c_eeprom_read_byte.c all describe a piece of
 * it and point here for the whole.
 *
 * First, the bit is write-only. A scan of both listings for any instruction
 * naming bit 0x16 finds nine writes and no reads in each image: rev20 SETB at
 * 0x0456, 0x0E55, 0x0EC8 and CLR at 0x0468, 0x04FD, 0x0E5A, 0x0E5F, 0x0ECD,
 * 0x0ED2; rev22 SETB at 0x045C, 0x0E49, 0x0EBA and CLR at 0x046B, 0x0501,
 * 0x0E4E, 0x0E53, 0x0EBF, 0x0EC4. No JB, JNB, JBC, CPL or MOV C touches it in
 * either image. Nor is the containing byte read anywhere but here -- IRAM 0x22
 * is loaded only at rev20 0x0F0E (`MOV R5,0x22`) and rev22 0x0EFE
 * (`MOV R7,0x22`), the first instruction of this routine's payload fetch. So
 * 0x22.6 is not internal state that something later tests; it is an output
 * line, the second bit this routine clocks out, and writing it does exactly
 * one thing: decide what the panel latch sees at the next commit.
 *
 * Second, its nominal value is derived. Both button state machines recompute
 * it in the same three instructions at their tails -- button_a_cycle_3state
 * rev20 0x0E52-0x0E60 (rev22 panel_state_cycle_A, 0x0E46-0x0E54) and
 * button_b_cycle_3state rev20 0x0EC5-0x0ED3 (rev22 panel_state_cycle_B,
 * 0x0EB7-0x0EC5):
 *      JB  0x2C,+2 ; SETB 0x16     -- set when f_spdif is clear
 *      JNB 0x2C,+2 ; CLR  0x16     -- clear when f_spdif is set
 *      JNB 0x2D,+2 ; CLR  0x16     -- clear when f_force is set
 * that is, 0x22.6 = !(f_spdif | f_force), with f_spdif = bit 0x2C = IRAM
 * 0x25.4 and f_force = bit 0x2D = IRAM 0x25.5.
 *
 * Third, the other two writers do not contradict that rule; they short-circuit
 * it. cmd4_variantA_reapply_mode (rev20 0x0454, rev22 0x045A) clears f_spdif
 * and sets 0x16 in the very next instruction, and cmd5_variantB_set_mode1
 * (rev20 0x0466, rev22 0x0469) sets f_spdif and clears 0x16 -- in both cases
 * the derived value written straight out rather than recomputed, because the
 * command already knows which way it went. cmd11_eeprom_selftest (rev20
 * 0x04C4, rev22 0x04C8) sets f_force first (rev20 0x04CA, rev22 0x04CE), so
 * from that instant the rule says 0x16 belongs clear; it then clears it at
 * rev20 0x04FD / rev22 0x0501 only on the PASS branch of the read-back
 * compare, and commits chain A immediately after. A FAILing self-test leaves
 * the bit wherever it was -- the one path in either image that knowingly
 * leaves the latch disagreeing with the derived value, which is precisely how
 * the failure is made visible.
 *
 * So there is one meaning, not three: bit 6 of the chain-A latch byte,
 * normally carrying !(f_spdif | f_force). Whether the panel indicator on that
 * line reads to a user as "internal clock" or as "self-test passed" is not
 * decidable from the firmware; the self-test reuses the same lamp and only
 * updates it when the test passes.
 *
 * THE TAIL IS ASYMMETRIC, and that is stock, not a transcription slip. With
 * bit 0x1E clear it does the expected thing: drop the data line, pulse P1.6
 * high then low. With bit 0x1E set it instead drives P1.7 and P1.6 both high
 * with a single ORL 0x90,#0xC0 and returns, leaving them there.
 *
 * Bit 0x1E is IRAM 0x23 bit 6, toggled by toggle_bit1e (0x1028) when the P3.5
 * front-panel button is pressed (see p3_button_scan, 0x0ED5). What the pin
 * pair actually does in that held-high state is NOT established here -- the
 * firmware only shows the pin behaviour. An older note in this project called
 * 0x23.6 "48V phantom power"; nothing in either image supports that and it is
 * withdrawn. Note also that IRAM 0x23 is chain B's first payload byte, so its
 * bit 6 is simultaneously shifted out by shiftreg16_commit; the two uses are
 * the same storage, deliberately or not.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY, for the same reason as shiftreg16_commit:
 * the loop body is Keil's inline expansion of the <intrins.h> intrinsic
 * `_crol_(x, 1)` -- a DJNZ countdown around a single RL A, with the operand
 * staged through R7 because that is _crol_'s parameter register. SDCC emits
 * one RL A for the equivalent C and no peephole rule short of expanding one
 * instruction into six specific ones would close it.
 *
 * Cross-check against rev 22: the same routine is at 0x0EFC, 53 bytes,
 * instruction-for-instruction identical except that rev22 keeps the working
 * byte in R7 from the start (`MOV R7,0x22`) and so does not pay for the
 * two-byte `MOV R7,0x05` each pass. Every P1 mask and the 0x1E test are
 * unchanged (rev22 0x0F20: JNB 0x1e; 0x0F23: ORL 0x90,#0xC0).
 */
void shiftreg8_commit(void) __naked {
    __asm
        mov   r6,#0x08             ; 8 bits
        mov   r5,0x22              ; g_mux_byte -- the panel source code
        anl   0x90,#0xbf           ; P1.6 = 0 before shifting

    bitloop$:
        ; --- _crol_(r5, 1): old bit 7 ends up in bit 0 ----------------------
        mov   r0,#0x01             ; rotate count
        mov   r7,0x05              ; R7 <- R5 (the intrinsic's parameter reg)
        mov   a,r7
        inc   r0
        sjmp  rotest$
    rotate$:
        rl    a
    rotest$:
        djnz  r0,rotate$
        mov   r5,a

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
