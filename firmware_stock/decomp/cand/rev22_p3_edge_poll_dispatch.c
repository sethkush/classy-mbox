// MATCH: image=rev22 addr=0x0F31 len=51 func=p3_edge_poll_dispatch cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Front-panel button scan: sample P3, edge-detect P3.3/P3.4/P3.5 against the
 * previous sample, and run the matching state machine.  Rev 22 counterpart of
 * rev20 p3_button_scan (0x0ED5).
 *
 * Returns (in R7, Keil's convention) 1 if any button acted this pass, 0
 * otherwise.  The only caller is the main loop, rev22 0x0A89, which uses it as
 * a dirty flag: on 1 it calls shiftreg_out8_p1hi (0x0A90) and
 * shiftreg_out16_p1 (0x0A93) to push the new panel state out to hardware.
 * This is the whole of the front-panel input path -- there is no interrupt on
 * these pins, P3 is polled.
 *
 * IRAM 0x20 is the previous P3 sample, written at the exit (0x0F61).  It is a
 * bit-addressable byte and the function uses it both ways in the same breath:
 * `CJNE A,0x20` compares the BYTE, while `JB 0x03` / `JB 0x04` / `JB 0x05`
 * test BIT addresses 0x03/0x04/0x05 -- IRAM 0x20 bits 3, 4 and 5, the previous
 * state of exactly those three buttons.  (The 8051 trap in full: the same
 * literal "0x20" is bit address 0x20 = IRAM 0x24.0 elsewhere, an unrelated
 * flag.)
 *
 * EDGE POLARITY.  Each button acts when the previous sample read 0 and the
 * current sample reads 1 -- `JB prev,skip` then `JNB ACC.n,skip` -- i.e. on
 * the LOW-to-HIGH transition.  The port idles high, so a press pulls the pin
 * low and the action lands on button *release*.  That is what the encoding
 * says; not checked against hardware.
 *
 * Button map (rev22 addresses):
 *      P3.5 -> toggle_bit1E_state   (0x1020)  strobe-mode toggle, IRAM 0x23.6
 *      P3.3 -> panel_state_cycle_A  (0x0E1B)  channel A source ring
 *      P3.4 -> panel_state_cycle_B  (0x0E8F)  channel B source ring
 * Note the dispatch order is P3.5 first, then P3.3, then P3.4 -- not pin
 * order.
 *
 * The early exit is a whole-port comparison: if P3 is bit-for-bit what it was
 * last pass, nothing can have changed, so the function returns 0 immediately
 * and does not even rewrite the shadow (it is already correct).  The three
 * per-button tests only run on a pass where something moved.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY.  Two Keil-only conventions appear: the
 * return value is delivered in R7, and the accumulating flag is OR-ed into
 * that register by its bank-0 direct address (`ORL 0x07,#1` is R7 |= 1, not a
 * write to some IRAM global at 0x07).  SDCC returns chars in DPL and has no
 * way to express either.
 *
 * REV 20 -> REV 22 DELTA: four bytes, a register-allocation change with no
 * behavioural difference.  Rev 20 built the "acted" flag in R6 (`ORL 0x06,#1`)
 * and held the P3 sample in R5, then had to move the flag into the return
 * register at the end (`MOV R7,0x06`, 2 bytes) and to load R7 explicitly with
 * 0 on the early-exit path (`MOV R7,#0x00`, 2 bytes).  Rev 22 swapped the two
 * locals: the flag is built directly in R7 (`ORL 0x07,#1` at 0x0F44, 0x0F51,
 * 0x0F5E) and the sample lives in R6, so the `CLR A` / `MOV R7,A` at entry
 * already establishes the zero return and both extra instructions vanish.
 * 55 bytes -> 51.  Same three pins, same order, same polarity, same shadow
 * byte at IRAM 0x20, same three callees.
 */
void p3_edge_poll_dispatch(void) __naked {
    __asm
        .globl _toggle_bit1E_state
        .globl _panel_state_cycle_A
        .globl _panel_state_cycle_B

        clr   a
        mov   r7,a                 ; acted = 0, and this is the return value
        mov   r6,0xb0              ; sample P3 once; every test uses this copy
        mov   a,r6
        cjne  a,0x20,changed$      ; identical to the previous sample?
        ret                        ; nothing moved -> return 0 (R7 already 0)

    changed$:
        ; --- P3.5: strobe-mode toggle ---------------------------------------
        jb    0x05,a_btn$          ; previous P3.5 already high -> no edge
        mov   a,r6                 ; reload: the LCALLs below clobber A
        jnb   0xe5,a_btn$          ; current P3.5 low -> no edge
        lcall _toggle_bit1E_state
        orl   0x07,#0x01           ; R7 |= 1

    a_btn$:
        ; --- P3.3: channel A source ring ------------------------------------
        jb    0x03,b_btn$
        mov   a,r6
        jnb   0xe3,b_btn$
        lcall _panel_state_cycle_A
        orl   0x07,#0x01

    b_btn$:
        ; --- P3.4: channel B source ring ------------------------------------
        jb    0x04,out$
        mov   a,r6
        jnb   0xe4,out$
        lcall _panel_state_cycle_B
        orl   0x07,#0x01

    out$:
        mov   0x20,r6              ; new previous-sample shadow
        ret
    __endasm;
}
