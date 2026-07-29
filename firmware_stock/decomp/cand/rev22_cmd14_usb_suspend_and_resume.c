// MATCH: image=rev22 addr=0x0525 len=66 span=1 func=cmd14_usb_suspend_and_resume cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 14, Rev 22 -- USB suspend. Posted by usb_susr_handler (0x0006) from
 * the SUSR interrupt and run later from the main-loop dispatcher, NOT in
 * interrupt context, because the middle of this function parks the CPU.
 * Counterpart of Rev 20's evt0e_usb_suspend_enter_and_resume at 0x0526.
 *
 * The single most important thing about it: `ORL 0x87,#0x1` at 0x0542 is
 * PCON.IDL. The CPU stops there. Everything AFTER that instruction is the
 * RESUME path, executed when an interrupt wakes the core -- which is why the
 * function reads as two unrelated halves.
 *
 * Suspend half:
 *   - guard: do nothing unless bit 0x0E (IRAM 0x21.6, configured) or bit 0x0A
 *     (IRAM 0x21.2) is set. An unconfigured device has nothing to power down.
 *     Keil evaluated the `||` in the carry flag -- MOV C,bit / ORL C,bit /
 *     JNC -- rather than as two branches. NOTE the bit-vs-byte trap: BIT 0x0A
 *     is IRAM 0x21.2, whereas the dispatcher's `MOV A,0x0A` reads BYTE 0x0A,
 *     the pending-event code. Different storage, same printed number;
 *   - ACGCTL &= 0x3F (0xFFE1): clears the top two bits, stopping both audio
 *     clock generators, so the codec port stops toggling while suspended;
 *   - panel parked: chain B's two payload bytes IRAM 0x25 and 0x23 are zeroed
 *     and pushed with shiftreg_out16_p1 (payload is 0x23 then 0x25, MSB
 *     first). Chain A's payload is the single byte IRAM 0x22, set to 0xFF and
 *     pushed with shiftreg_out8_p1hi.
 *
 *     The `CLR 0x1E` between the two is NOT chain-A payload. Bit 0x1E is IRAM
 *     0x23.6, and the 8-bit commit READS it to pick which of two tails it
 *     runs: clear gives the normal drop-data-then-pulse-strobe ending, set
 *     leaves P1.7 and P1.6 driven high (cand/shiftreg8_commit.c). Clearing it
 *     here selects the normal strobe. It is also bit 6 of chain B's first
 *     payload byte, which this function has just zeroed anyway, so the write
 *     is redundant as chain-B state and load-bearing only as the strobe-mode
 *     selector.
 *
 *     What 0xFF *means* on chain A is not established. The master init ends
 *     with the same `MOV 0x22,#0xFF` plus three bit clears, which suggests an
 *     idle/park value, but cand/cmd2_apply_iface1_alt.c writes it when a
 *     stream STARTS, so "all off" cannot be right as a general reading. Treat
 *     it as "the known park value", nothing more.
 *
 * Resume half, in order:
 *   - USBCTL &= 0x7F: drop CONN. The device detaches from the bus;
 *   - USBIMSK = 0x9F, written through INC DPTR from USBCTL's 0xFFFC to
 *     0xFFFD -- the same reduced mask the bus-reset handler writes:
 *     RSTR(7) | SOF(4) | PSOF(3) | SETUP(2) | bit 1 | STPOW(0), with SUSR(6)
 *     and RESR(5) masked off again;
 *   - hw_clock_codec_init (0x07EC) and usb_ep_dma_init (0x0891) re-run in full;
 *   - TR0 (bit 0x8C = TCON.4) restarts the panel tick, EX0 (0xA8) re-enables
 *     the USB engine interrupt, EA (0xAF) re-enables interrupts;
 *   - USBCTL |= 0x80: CONN again. So the resume path is a deliberate
 *     re-enumeration -- the Mbox comes back as a freshly attached device
 *     rather than restoring the pre-suspend configuration.
 *
 * WHY THIS CANDIDATE IS 66 BYTES AND CARRIES span=1. Ghidra's function runs
 * 0x0525..0x0566, and the last four bytes of that range are the dispatcher
 * epilogue at 0x0563 -- `CLR A / MOV 0x0A,A / RET`, the shared "clear the
 * pending event" tail that the dispatcher's default arm and eight other case
 * bodies jump to. In Rev 20 that tail was far enough out to be its own Ghidra
 * function (0x0564, cand/evt_dispatch_epilogue.c) and evt0e stopped at 0x0563;
 * in Rev 22 it is inside this function's extent, so this candidate claims it
 * and it needs a symbols.map entry-point row (see proposed/cmdrest.symbols).
 * The suspend body itself is still exactly 62 bytes, 0x0525..0x0562, and still
 * FALLS THROUGH into the epilogue rather than returning -- both the guard's
 * JNC at 0x0529 and the end of the resume path land on 0x0563.
 *
 * WRITTEN AS ASSEMBLY, for three reasons at once. (1) The fall-through: SDCC
 * cannot end a function by running off its end into the next thing. (2) The
 * carry-flag `||`: SDCC lowers `if (a || b)` to two conditional branches,
 * never to MOV C / ORL C. (3) The INC DPTR at 0x054C carrying USBCTL's DPTR to
 * USBIMSK. That is the hw_master_init class (decomp/README.md, "When to stop
 * using C").
 *
 * REV 20 -> REV 22 DELTA: BEHAVIOUR IDENTICAL, ONLY CALL OPERANDS MOVED.
 * The 62-byte suspend body is structurally identical instruction for
 * instruction, including the same JNC displacement 0x38. Four call targets
 * relocated: 0x0E56 / 0x0EFC / 0x07EC / 0x0891 here against Rev 20's 0x0E62 /
 * 0x0F0C / 0x08CB / 0x0970. Both revisions write USBIMSK = 0x9F here, both
 * ACGCTL &= 0x3F, both park chain A at 0xFF, both re-enumerate on wake.
 * Suspend/resume is NOT what Rev 22 changed.
 *
 * (Recorded for completeness because USBIMSK is a live topic in this project:
 * the 0x9F written here has bit 4 = SOF SET. The masked-off-SOF problem in the
 * project memory is about our own firmware's 0xE5, not about this write, and
 * both stock revisions agree on 0x9F.) */
void cmd14_usb_suspend_and_resume(void) __naked {
    __asm
        .globl _shiftreg_out16_p1
        .globl _shiftreg_out8_p1hi
        .globl _hw_clock_codec_init
        .globl _usb_ep_dma_init

        mov   c,0x0e               ; BIT 0x0E = IRAM 0x21.6 -- configured
        orl   c,0x0a               ; BIT 0x0A = IRAM 0x21.2
        jnc   0001$                ; neither: nothing to suspend

        mov   dptr,#0xffe1         ; ACGCTL
        movx  a,@dptr
        anl   a,#0x3f              ; stop both audio clock generators
        movx  @dptr,a

        clr   a
        mov   0x25,a               ; chain B payload, second 8 bits
        mov   0x23,a               ; chain B payload, first 8 bits
        lcall _shiftreg_out16_p1
        mov   0x22,#0xff           ; chain A payload, park value
        clr   0x1e                 ; BIT 0x1E = IRAM 0x23.6 -- selects the
                                   ; normal strobe tail, not payload
        lcall _shiftreg_out8_p1hi

        orl   0x87,#0x01           ; PCON.IDL -- the CPU stops HERE
                                   ; ---- everything below runs on wake ----

        mov   dptr,#0xfffc         ; USBCTL
        movx  a,@dptr
        anl   a,#0x7f              ; drop CONN: detach
        movx  @dptr,a
        inc   dptr                 ; -> 0xFFFD USBIMSK, DPTR still live
        mov   a,#0x9f              ; RSTR(7)|SOF(4)|PSOF(3)|SETUP(2)|bit1|STPOW(0)
        movx  @dptr,a

        lcall _hw_clock_codec_init ; full hardware bring-up, again
        lcall _usb_ep_dma_init

        setb  0x8c                 ; TCON.4 TR0 -- panel tick running
        setb  0xa8                 ; IE.0 EX0 -- USB engine interrupt
        setb  0xaf                 ; IE.7 EA

        mov   dptr,#0xfffc         ; USBCTL
        movx  a,@dptr
        orl   a,#0x80              ; CONN: re-attach, host re-enumerates
        movx  @dptr,a

        ;; ---- merged tail @ 0x0563: evt_dispatch_epilogue ------------------
        ;; `g_event = 0; return;` -- the switch's common exit. Reached by
        ;; fall-through from above, by the guard's JNC, by the dispatcher's
        ;; default arm (0x02FB) and by eight other case bodies.
    0001$:
        clr   a
        mov   0x0a,a               ; BYTE 0x0A = g_event: nothing pending
        ret
    __endasm;
}
