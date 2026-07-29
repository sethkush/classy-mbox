// MATCH: image=rev20 addr=0x0E62 len=59 func=shiftreg16_commit cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Front-panel shift-register chain B: clock 16 bits out on P1, then latch.
 *
 * Ghidra calls this shiftreg16_commit_p1_0_1_2; the C name is the one the rest
 * of the decompilation calls it by (firmware_stock/decomp/symbols.map), because
 * hw_master_init and eleven other sites reference it.
 *
 * Pin assignment, read straight off the read-modify-writes below:
 *      P1.0  serial data   (ORL 0x90,#0x01 / ANL 0x90,#0xFE)
 *      P1.2  shift clock   (ORL 0x90,#0x04 then ANL 0x90,#0xFB -- pulse high)
 *      P1.1  latch strobe  (ORL 0x90,#0x02 then ANL 0x90,#0xFD, once, at end)
 * Data is presented, then the clock is pulsed, so the register samples on the
 * rising edge of P1.2. The latch pulse comes after all 16 bits, so the outputs
 * change once, atomically, rather than rippling.
 *
 * Payload, MSB first: IRAM 0x23 (g_panel_lo) then IRAM 0x25 (g_panel_hi).
 * That ordering means g_panel_lo's bit 7 is furthest down the chain.
 * Bit address 0x30 -- IRAM 0x26 bit 0 -- is the loop's own "second byte still
 * to go" flag: set on entry, tested when the bit counter hits zero, and
 * cleared as the second byte is loaded. It is not panel state.
 *
 * Note the byte at IRAM 0x25 doubles as the bit-addressable home of the panel
 * flags: bit 0x2C (f_spdif) is 0x25.4, 0x2D (f_force) is 0x25.5, and bit 0x2F
 * -- 0x25.7 -- is the chip-select that cs8427_ctl_write (0x0C45) asserts by
 * clearing it and then calling straight into here. So this routine is the only
 * thing that actually drives that select line onto a pin.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY. The rotate at the top of the loop is
 * Keil's inline expansion of the <intrins.h> intrinsic `_crol_(x, 1)`:
 *
 *      MOV R0,#1 / MOV A,x / INC R0 / SJMP L2 / L1: RL A / L2: DJNZ R0,L1
 *
 * -- a count-down loop around RL A, emitted even though the count is the
 * constant 1, with the operand loaded from R7 because that is where _crol_'s
 * first parameter lives. SDCC has no such intrinsic and compiles
 * `(x<<1)|(x>>7)` to a single RL A, six bytes shorter. Reproducing the stock
 * encoding would need a peephole rule that *expands* one instruction into a
 * specific six-instruction sequence -- exactly the one-adjacency rule the
 * project's rule of thumb says to write as assembly instead.
 *
 * The R5 -> R7 -> A -> R5 shuffle inside the loop is the visible cost of that
 * intrinsic: the working byte lives in R5 and has to be copied to R7 for each
 * call. Rev 22 allocated it in R7 to begin with (0x0E56, `MOV R7,0x23`) and so
 * dropped the two-byte `MOV R7,0x05`, making its version 57 bytes against this
 * one's 59. The two are otherwise instruction-for-instruction identical.
 */
void shiftreg16_commit(void) __naked {
    __asm
        mov   r6,#0x08             ; 8 bits in this byte
        mov   r5,0x23              ; g_panel_lo -- first byte out
        setb  0x30                 ; IRAM 0x26.0: a second byte follows

    bitloop$:
        mov   a,r6
        jz    bytedone$            ; bit counter exhausted

        ; --- _crol_(r5, 1): rotate left one, old bit 7 lands in bit 0 -------
        mov   r0,#0x01             ; rotate count
        mov   r7,0x05              ; R7 <- R5 (the intrinsic's parameter reg)
        mov   a,r7
        inc   r0
        sjmp  rotest$
    rotate$:
        rl    a
    rotest$:
        djnz  r0,rotate$
        mov   r5,a                 ; keep the rotated byte for the next bit

        ; --- present bit 0 of the rotated byte on P1.0 ----------------------
        jnb   0xe0,data0$          ; ACC.0 -- the bit just rotated out of 7
        orl   0x90,#0x01           ; P1.0 = 1
        sjmp  clock$
    data0$:
        anl   0x90,#0xfe           ; P1.0 = 0
    clock$:
        orl   0x90,#0x04           ; P1.2 high  -- register samples data here
        anl   0x90,#0xfb           ; P1.2 low
        dec   r6
        sjmp  bitloop$

    bytedone$:
        jnb   0x30,latch$          ; second byte already sent?
        clr   0x30
        mov   r5,0x25              ; g_panel_hi -- second byte out
        mov   r6,#0x08
        sjmp  bitloop$

    latch$:
        orl   0x90,#0x02           ; P1.1 high: transfer shift stage to outputs
        anl   0x90,#0xfd           ; P1.1 low
        ret
    __endasm;
}
