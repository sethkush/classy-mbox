// MATCH: image=rev22 addr=0x0E56 len=57 func=shiftreg_out16_p1 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Front-panel shift-register chain B: clock 16 bits out on the low nibble of
 * P1, then latch them.  Rev 22 counterpart of rev20 shiftreg16_commit
 * (0x0E62).  Eight call sites in rev22 (0x0384, 0x088D, 0x09C1, 0x09DC,
 * 0x09E7, 0x09F0, 0x09F5, 0x0A93).
 *
 * Pin assignment, read straight off the read-modify-writes below:
 *      P1.0  serial data   (ORL 0x90,#0x01 / ANL 0x90,#0xFE)
 *      P1.2  shift clock   (ORL 0x90,#0x04 then ANL 0x90,#0xFB -- pulse high)
 *      P1.1  latch strobe  (ORL 0x90,#0x02 then ANL 0x90,#0xFD, once, at end)
 * Data is presented before the clock is pulsed, so the register samples on the
 * rising edge of P1.2.  The latch pulse comes after all 16 bits, so the panel
 * outputs change once, atomically, rather than rippling through intermediate
 * states.
 *
 * Payload, MSB first: IRAM 0x23 then IRAM 0x25.  IRAM 0x23 bit 6 is the
 * strobe-mode toggle bit 0x1E that toggle_bit1E_state (0x1020) flips, so that
 * bit is clocked out here as well as being tested by shiftreg_out8_p1hi.
 * IRAM 0x25 is the bit-addressable home of the panel flags: f_spdif is 0x25.4
 * (bit 0x2C), f_force is 0x25.5 (bit 0x2D), and 0x25.7 (bit 0x2F) is the
 * chip-select that cs8427_write_shadowed asserts by clearing it and then
 * calling into here -- this routine is the only thing that drives that select
 * line onto a pin.
 *
 * Bit address 0x30 -- IRAM 0x26 bit 0 -- is the loop's own "second byte still
 * to go" flag: set on entry (0x0E5A), tested when the bit counter reaches zero
 * (0x0E7D), cleared as the second byte is loaded (0x0E80).  It is not panel
 * state and nothing outside this function touches it.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY, for the same reason as the Rev 20
 * counterpart: the rotate at the top of the loop is Keil's inline expansion of
 * the <intrins.h> intrinsic `_crol_(x, 1)` --
 *      MOV R0,#1 / MOV A,x / INC R0 / SJMP L2 / L1: RL A / L2: DJNZ R0,L1
 * a count-down loop around RL A emitted even though the count is the constant
 * 1.  SDCC compiles the equivalent C to a single RL A, and reproducing the
 * stock encoding would need a peephole rule that *expands* one instruction
 * into six specific ones -- exactly the one-adjacency rule the project's rule
 * of thumb says to write as assembly instead.
 *
 * REV 20 -> REV 22 DELTA: two bytes, and it is a register-allocation change,
 * not a behaviour change.  Rev 20 kept the working byte in R5 and had to copy
 * it into R7 for every call of the intrinsic, paying a two-byte
 * `MOV R7,0x05` (R7 <- R5 by direct address) each pass and a `MOV R5,A` to put
 * the result back.  Rev 22 allocated the working byte in R7 from the start
 * (0x0E58 `MOV R7,0x23`, 0x0E82 `MOV R7,0x25`), so the copy disappears and
 * `MOV R5,A` becomes `MOV R7,A`.  59 bytes -> 57.  The only other consequence
 * is that the two backward SJMP displacements into the loop head shift by two
 * (rev20 80 dd / 80 d2, rev22 80 df / 80 d4).  Every P1 mask, both payload
 * addresses, the bit ordering and the 0x30 flag are identical.
 */
void shiftreg_out16_p1(void) __naked {
    __asm
        mov   r6,#0x08             ; 8 bits in this byte
        mov   r7,0x23              ; first byte out (holds the 0x1E toggle bit)
        setb  0x30                 ; IRAM 0x26.0: a second byte follows

    bitloop$:
        mov   a,r6
        jz    bytedone$            ; bit counter exhausted

        ; --- _crol_(r7, 1): rotate left one, old bit 7 lands in bit 0 -------
        mov   r0,#0x01             ; rotate count
        mov   a,r7                 ; working byte already lives in R7 (rev22)
        inc   r0
        sjmp  rotest$
    rotate$:
        rl    a
    rotest$:
        djnz  r0,rotate$
        mov   r7,a                 ; keep the rotated byte for the next bit

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
        mov   r7,0x25              ; second byte out (panel flag byte)
        mov   r6,#0x08
        sjmp  bitloop$

    latch$:
        orl   0x90,#0x02           ; P1.1 high: transfer shift stage to outputs
        anl   0x90,#0xfd           ; P1.1 low
        ret
    __endasm;
}
