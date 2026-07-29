// MATCH: image=rev20 addr=0x101E len=10 func=timer0_isr_tick cflags=--peep-file,firmware_stock/decomp/keil.peep

/* Timer-0 overflow ISR -- the front-panel tick. Reached by LJMP from the
 * timer-0 vector at 0x000B (0x000B: 02 10 1e).
 *
 * All it does is raise a flag and reload the timer. The work it gates is in
 * the main loop at 0x0AD3, which spins on `JB 0x20,0x0ADF` and, when the bit
 * is set, reads the buttons (0x0ED5), pushes both shift-register chains
 * (0x0F0C / 0x0E62), posts events 11 and 12 for the two button edges, and
 * clears the bit again at 0x0B0D. So bit 0x20 -- IRAM 0x24.0, not IRAM byte
 * 0x20 -- is "a panel scan is due", with the ISR as producer and the main
 * loop as sole consumer.
 *
 * TMOD was programmed to 0x11 in hw_master_init (0x08E5: MOV 0x89,#0x11), so
 * timer 0 is in 16-bit mode 1 with no auto-reload; the ISR reloads TH0 only
 * and lets TL0 free-run from wherever it happened to be, which makes the
 * period 0x3200 timer counts plus whatever TL0 held -- a deliberately sloppy
 * "roughly every 12800 counts" scan rate, not a precise timebase.
 *
 * WRITTEN AS ASSEMBLY. This is an ISR that saves nothing: the body touches
 * only bit-addressable SFRs and one `MOV direct,#imm`, so no register is
 * disturbed and Keil emitted no context save at all. SDCC's `__interrupt`
 * always frames the function (and emits its own vector entry), and a plain C
 * function cannot end in RETI. Ten bytes of flag-and-reload is the same code
 * either way, so it is written out directly.
 *
 * The `CLR EA` / `SETB EA` pair around the body is what Keil emits for an
 * explicit EA=0/EA=1 bracket in the source. Note that it is NOT the case that
 * EA is already clear on entry: the 8051 does not clear EA when it vectors, it
 * sets a priority-level in-service latch, and EA stays as it was (which here is
 * 1, since the main loop is running with interrupts on).
 *
 * The pair is still a no-op, for a different reason. hw_master_init leaves
 * exactly two interrupt sources enabled -- ET0 and EX0 (rev20 0x08F3 SETB 0xA9,
 * 0x08F7 SETB 0xA8; rev22 0x0814 / 0x0818), with ES, EX1 and ET1 explicitly
 * cleared -- and then writes IP = 0 (rev20 0x08F9, rev22 0x081A `MOV 0xB8,A`
 * with A == 0), so both sit at priority level 0. Entering this ISR raises the
 * level-0 in-service latch, which already blocks both of them until the RETI.
 * So there is nothing left for CLR EA to prevent; the source said "critical
 * section" and Keil obliged. The `SETB EA` on the way out is likewise just
 * restoring a bit that was already 1.
 *
 * Rev 22 has these same ten bytes at 0x1016 (c2 af d2 20 75 8c ce d2 af 32),
 * reached from its own timer-0 vector at 0x000B (02 10 16). */
void timer0_isr_tick(void) __naked {
    __asm
        clr   0xaf         ; EA = 0
        setb  0x20         ; IRAM 0x24.0 -- panel scan due
        mov   0x8c,#0xce   ; TH0 reload; TL0 is left alone
        setb  0xaf         ; EA = 1
        reti
    __endasm;
}
