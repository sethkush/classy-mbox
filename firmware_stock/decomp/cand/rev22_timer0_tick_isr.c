// MATCH: image=rev22 addr=0x1016 len=10 func=timer0_tick_isr cflags=--peep-file,firmware_stock/decomp/keil.peep

/* Timer-0 overflow ISR, Rev 22 at 0x1016. Reached by LJMP from the timer-0
 * vector at 0x000B (rev22 0x000B: 02 10 16; rev20 0x000B: 02 10 1e).
 *
 * REV 20 -> REV 22 DELTA: NONE. All ten bytes are identical --
 * rev20_firmware_code.bin[0x101E:0x1028] and
 * rev22_firmware_code.bin[0x1016:0x1020] are both
 * c2 af d2 20 75 8c ce d2 af 32. Only the address moved, and it moved because
 * the code above it grew, not because this function changed. There are no
 * address operands in the body at all (the only two-byte operand is
 * `MOV 0x8C,#0xCE`, an SFR and an immediate), so relocation could not have
 * perturbed it even if it had wanted to. The Rev 20 candidate
 * cand/timer0_isr_tick.c ported verbatim; only the MATCH header changed.
 *
 * WHAT IT DOES. Raise a flag and reload the timer, nothing else. The work it
 * gates lives in the Rev 22 main loop: `JB 0x20,...` at rev22 0x0A75 (rev20
 * 0x0AD3), which on the set bit scans the front-panel buttons, clocks both
 * shift-register chains out, posts the two button-edge events, and clears the
 * bit again. Bit 0x20 is IRAM 0x24.0 -- NOT IRAM byte 0x20 -- so this is a
 * single-bit "a panel scan is due" handshake with the ISR as sole producer and
 * the main loop as sole consumer.
 *
 * TMOD was programmed to 0x11 in hw_master_init (rev22 0x0806, rev20 0x08E5:
 * `MOV 0x89,#0x11`), so timer 0 is 16-bit mode 1 with no auto-reload. The ISR
 * reloads TH0 (SFR 0x8C) only and lets TL0 free-run from wherever it happened
 * to be when the overflow hit, which makes the period 0x3200 counts plus
 * whatever TL0 held. That is a deliberately approximate scan rate, not a
 * timebase -- nothing in either image measures time with it.
 *
 * THE CLR EA / SETB EA BRACKET IS A NO-OP, and it is worth saying why rather
 * than assuming. It is not that EA is already clear on entry: the 8051 does
 * not clear EA when it vectors, it sets a priority-level in-service latch, and
 * EA keeps whatever value it had (1 here, since the main loop runs with
 * interrupts on). It is that hw_master_init leaves exactly two sources enabled
 * -- ET0 and EX0 (rev22 0x0814 `SETB 0xA9`, rev22 0x0818 `SETB 0xA8`; rev20
 * 0x08F3 / 0x08F7), with ES, EX1 and ET1 explicitly cleared -- and then writes
 * IP = 0 (rev22 0x081A, rev20 0x08F9, `MOV 0xB8,A` with A == 0), so both sit
 * at priority level 0. Entering this ISR raises the level-0 in-service latch,
 * which already blocks both of them until the RETI. The source said "critical
 * section" and Keil obliged.
 *
 * WRITTEN AS ASSEMBLY. The body touches only bit-addressable SFRs and one
 * `MOV direct,#imm`, so no register is disturbed and Keil emitted no context
 * save at all. SDCC's `__interrupt` always frames the function and emits its
 * own vector entry, neither of which can be steered to this, and a plain C
 * function cannot end in RETI. */
void timer0_tick_isr(void) __naked {
    __asm
        clr   0xaf         ; EA = 0
        setb  0x20         ; IRAM 0x24.0 -- panel scan due
        mov   0x8c,#0xce   ; TH0 reload; TL0 is left alone
        setb  0xaf         ; EA = 1
        reti
    __endasm;
}
