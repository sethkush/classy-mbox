// MATCH: image=rev20 addr=0x0ED5 len=55 func=p3_button_scan cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Front-panel button scan: edge-detect P3.3/P3.4/P3.5 against the previous
 * sample and run the matching selector state machine.
 *
 * Returns (in R7, Keil's convention) 1 if any button acted this pass, 0
 * otherwise. The only caller, the main loop at 0x0ADF, uses that as a dirty
 * flag: on 1 it calls shiftreg8_commit (0x0AE6) and shiftreg16_commit
 * (0x0AE9) to push the new panel state out to hardware. This is therefore the
 * whole of the front-panel input path -- there is no interrupt on these pins,
 * P3 is polled.
 *
 * IRAM 0x20 is the previous P3 sample, written at the exit (0x0F07). It is
 * also a bit-addressable byte, and the function uses it both ways in the same
 * breath: `CJNE A,0x20` compares the byte, while `JB 0x03` / `JB 0x04` /
 * `JB 0x05` test bit addresses 0x03/0x04/0x05, which are IRAM 0x20 bits 3, 4
 * and 5 -- the previous state of exactly the three buttons. (The 8051 trap
 * applies here in full: the same "0x20" that is a byte in the CJNE is bit 0x20
 * = IRAM 0x24.0 in the main loop's `JB 0x20` at 0x0AD3, an unrelated flag.)
 *
 * This function is not the only reader of that shadow. A byte-scan of both
 * images for bit operations on addresses 0x00-0x07 finds exactly five sites in
 * each, and the two that are not here belong to the main loop: rev20 0x0AEC
 * `JB 0x01` and 0x0AFC `JNB 0x01` (rev22 0x0A96 / 0x0AA6) read bit 1 of the
 * same byte -- the previous P3.1 sample. That pin is handled on level, not
 * edge, with IRAM 0x27 as a one-shot latch: P3.1 low with 0x27 == 0 queues
 * event 0x0B (0x0AF6), P3.1 high with 0x27 == 1 queues event 0x0C (0x0B07).
 * So P3.1 is a maintained input rather than a momentary button, and this scan
 * is what samples it for that code. No site in either image ever writes bits
 * 0x00-0x07 individually; the only producer is the whole-byte store below.
 *
 * EDGE POLARITY. Each button acts when the previous sample read 0 and the
 * current sample reads 1 -- `JB prev,skip` then `JNB ACC.n,skip` -- i.e. on
 * the LOW-to-HIGH transition. The pins idle LOW and a press drives them HIGH,
 * so the action lands on the PRESS.
 *
 * CORRECTED 2026-08-03. This said "hw_master_init writes P3 = 0xFF, so the
 * pins idle high and a press pulls them low; the action therefore lands on
 * button *release*", with the honest caveat that it had not been checked on
 * hardware. The encoding was read correctly and the inference from P3 = 0xFF
 * was wrong: that write sets the port LATCH, which is what makes the pin an
 * input -- it does not decide what the external network does with it. And
 * hw_master_init also sets GLOBCTL bit 1 (P3PUDIS) at 0x08FE, releasing the
 * internal pull-ups entirely, so the board drives these pins, not the chip.
 *
 * The image settles it without a meter. The shadow at IRAM 0x20 is zeroed by
 * Keil's ?C_INITSEG table (cand/c51_initseg_table.c, record `01 20 00`), so on
 * the first scan after boot `prev` is 0 for all three buttons. Were the pins
 * idle-high, all three handlers would fire on that first scan of every boot:
 * both channels would step MIC->LINE and bit 0x1E would toggle, before the
 * user touched anything. hw_master_init seeds the panel word to 0xF6 = MIC on
 * both channels and the hardware is observed to boot to MIC and stay there.
 * Therefore the pins read 0 at rest. The buttons are ACTIVE HIGH.
 *
 * This mattered downstream: mboxfw copied the active-low reading into
 * regs.h, buttons.c and its boot-time DFU escape, left P3PUDIS clear on the
 * strength of a misread bisect, and its buttons were dead on hardware. See
 * FINDING_buttons_are_active_high.md.
 *
 * Button map:
 *      P3.3 -> button_a_cycle_3state (0x0E27)  channel A source ring
 *      P3.4 -> button_b_cycle_3state (0x0E9D)  channel B source ring
 *      P3.5 -> toggle_bit1e          (0x1028)  toggles IRAM 0x23.6
 * Bit 0x1E is a plain toggle whose only consumer is the tail of
 * shiftreg8_commit (0x0F32), where it selects between a normal P1.6 strobe and
 * holding P1.7/P1.6 high. It is not phantom power; an older note in this
 * project said so and was wrong.
 *
 * The early exit is a whole-port comparison: if P3 is bit-for-bit what it was
 * last pass, nothing can have changed and the function returns 0 immediately.
 * The three per-button tests only run on a pass where something moved.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY. Two Keil-only conventions appear: the
 * return value is delivered in R7, and the accumulating flag lives in R6
 * addressed by its bank-0 direct address (`ORL 0x06,#1` is R6 |= 1, not a
 * write to some IRAM global). SDCC returns chars in DPL and has no way to
 * express either.
 *
 * Rev 22 has the same function at 0x0F31, 51 bytes, structurally identical but
 * with the two locals swapped into R7/R6: the flag is built directly in the
 * return register (`ORL 0x07,#1` at 0x0F44/0x0F51/0x0F5E), so it needs neither
 * the `MOV R7,#0` on the early exit nor the `MOV R7,0x06` at the end. Same
 * three pins, same polarity, same P3 shadow at IRAM 0x20 (rev22 0x0F61).
 */
void p3_button_scan(void) __naked {
    __asm
        .globl _toggle_bit1e
        .globl _button_a_cycle_3state
        .globl _button_b_cycle_3state

        clr   a
        mov   r6,a                 ; acted = 0
        mov   r5,0xb0              ; sample P3 once; every test uses this copy
        mov   a,r5
        cjne  a,0x20,changed$      ; identical to the previous sample?
        mov   r7,#0x00             ; nothing moved -> return 0, and do not
        ret                        ;   even update the shadow (it is unchanged)

    changed$:
        ; --- P3.5: toggle bit 0x1E ------------------------------------------
        jb    0x05,a_btn$          ; previous P3.5 already high -> no edge
        mov   a,r5                 ; reload: the LCALLs below clobber A
        jnb   0xe5,a_btn$          ; current P3.5 low -> no edge
        lcall _toggle_bit1e
        orl   0x06,#0x01           ; R6 |= 1

    a_btn$:
        ; --- P3.3: channel A source ring ------------------------------------
        jb    0x03,b_btn$
        mov   a,r5
        jnb   0xe3,b_btn$
        lcall _button_a_cycle_3state
        orl   0x06,#0x01

    b_btn$:
        ; --- P3.4: channel B source ring ------------------------------------
        jb    0x04,out$
        mov   a,r5
        jnb   0xe4,out$
        lcall _button_b_cycle_3state
        orl   0x06,#0x01

    out$:
        mov   0x20,r5              ; new previous-sample shadow
        mov   r7,0x06              ; return R6
        ret
    __endasm;
}
