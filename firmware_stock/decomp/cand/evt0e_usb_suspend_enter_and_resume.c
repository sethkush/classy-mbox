// MATCH: image=rev20 addr=0x0526 len=62 func=evt0e_usb_suspend_enter_and_resume cflags=--peep-file,firmware_stock/decomp/keil.peep

/* Event 14 -- USB suspend. Posted by usb_ev_suspend (0x0006) from the SUSR
 * interrupt and run later from the main-loop dispatcher (LCALL at 0x0327), NOT
 * in interrupt context, because the middle of this function parks the CPU.
 *
 * The single most important thing about it: `ORL 0x87,#0x1` at 0x0543 is
 * PCON.IDL. The CPU stops there. Everything AFTER that instruction is the
 * RESUME path, executed when an interrupt wakes the core -- which is why the
 * function reads as two unrelated halves and why its Ghidra name has both
 * words in it.
 *
 * Suspend half:
 *   - guard: do nothing unless bit 0x0E (IRAM 0x21.6, configured) or bit 0x0A
 *     (IRAM 0x21.2) is set. Unconfigured devices have nothing to power down.
 *     Keil evaluated the `||` with the carry flag -- MOV C,bit / ORL C,bit /
 *     JNC -- rather than two branches;
 *   - ACGCTL &= 0x3F (0xFFE1): clears the top two bits, stopping both audio
 *     clock generators, so the codec port stops toggling while suspended;
 *   - panel parked: chain B's two payload bytes IRAM 0x25 and 0x23 are zeroed
 *     and pushed with shiftreg16_commit (cand/shiftreg16_commit.c: the payload
 *     is 0x23 then 0x25, MSB first; cand/audio_path_reconfig_ext_chips.c pins
 *     the same pair). Chain A's payload is the single byte IRAM 0x22
 *     (g_mux_byte), which is set to 0xFF and pushed with shiftreg8_commit.
 *
 *     The `CLR 0x1E` between the two is NOT chain-A payload. Bit 0x1E is IRAM
 *     0x23.6, and shiftreg8_commit READS it to pick which of two tails it
 *     runs: clear gives the normal drop-data-then-pulse-strobe ending, set
 *     leaves P1.7 and P1.6 driven high (cand/shiftreg8_commit.c). Clearing it
 *     here selects the normal strobe. It is also, as that file notes, bit 6 of
 *     chain B's first payload byte, which this function has just zeroed
 *     anyway, so the write is redundant as chain-B state and load-bearing only
 *     as the strobe-mode selector.
 *
 *     What 0xFF *means* on chain A is not established. hw_master_init ends
 *     with the same `MOV 0x22,#0xFF` plus three bit clears, which suggests an
 *     idle/park value, but cand/cmd2_apply_iface1_alt.c writes g_mux_byte =
 *     0xFF when a stream STARTS, so "all off" cannot be right as a general
 *     reading. Treat it as "the known park value", nothing more.
 *
 * Resume half, in order:
 *   - USBCTL &= 0x7F: drop CONN. The device detaches from the bus;
 *   - USBIMSK = 0x9F via INC DPTR, the same reduced mask the reset handler
 *     writes: RSTR(7) | SOF(4) | PSOF(3) | SETUP(2) | bit 1 | STPOW(0), with
 *     SUSR(6) and RESR(5) masked off again (cand/usb_rstr_handler.c);
 *   - hw_master_init (0x08CB) and usb_ep_dma_init (0x0970) re-run in full;
 *   - TR0 (bit 0x8C = TCON.4) restarts the panel tick, EX0 (0xA8) re-enables
 *     the USB engine interrupt, EA (0xAF) re-enables interrupts;
 *   - USBCTL |= 0x80: CONN again. So the resume path is a deliberate
 *     re-enumeration -- the Mbox comes back as a freshly attached device
 *     rather than restoring the pre-suspend configuration.
 *
 * FALLS THROUGH, it does not return. Both the guard's JNC at 0x052A and the
 * end of the resume path land on 0x0564, which is evt_dispatch_epilogue
 * (`CLR A / MOV 0x0A,A / RET`, the shared "clear the pending event" tail that
 * eight other event handlers LJMP to). Those three bytes belong to that
 * candidate, so this one claims exactly the 62 bytes 0x0526..0x0563 and ends
 * without a RET.
 *
 * WRITTEN AS ASSEMBLY, for three reasons at once. (1) The fall-through: SDCC
 * cannot end a function by running off its end into the next one, and the JNC
 * target is one past the last byte. (2) The carry-flag `||`: SDCC lowers
 * `if (a || b)` to two conditional branches, never to MOV C / ORL C. (3) The
 * INC DPTR at 0x054D carrying USBCTL's DPTR to USBIMSK. That is the
 * hw_master_init class (decomp/README.md, "When to stop using C").
 *
 * Rev 22 has the same 62-byte function at 0x0525, structurally identical, with
 * only the four call operands moved (0x0E56, 0x0EFC, 0x07EC, 0x0891 against
 * Rev 20's 0x0E62, 0x0F0C, 0x08CB, 0x0970) and the same JNC displacement 0x38
 * landing on its own evt_dispatch_epilogue at 0x0563. Both revisions write
 * USBIMSK = 0x9F here. */
void evt0e_usb_suspend_enter_and_resume(void) __naked {
    __asm
        .globl _shiftreg16_commit
        .globl _shiftreg8_commit
        .globl _hw_master_init
        .globl _usb_ep_dma_init

        mov   c,0x0e               ; IRAM 0x21.6 -- configured
        orl   c,0x0a               ; IRAM 0x21.2
        jnc   0001$                ; neither: nothing to suspend

        mov   dptr,#0xffe1         ; ACGCTL
        movx  a,@dptr
        anl   a,#0x3f              ; stop both audio clock generators
        movx  @dptr,a

        clr   a
        mov   0x25,a               ; chain B payload, second 8 bits
        mov   0x23,a               ; chain B payload, first 8 bits
        lcall _shiftreg16_commit
        mov   0x22,#0xff           ; chain A payload (g_mux_byte), park value
        clr   0x1e                 ; IRAM 0x23.6 -- selects shiftreg8_commit's
                                   ; normal strobe tail, not payload
        lcall _shiftreg8_commit

        orl   0x87,#0x01           ; PCON.IDL -- the CPU stops HERE
                                   ; ---- everything below runs on wake ----

        mov   dptr,#0xfffc         ; USBCTL
        movx  a,@dptr
        anl   a,#0x7f              ; drop CONN: detach
        movx  @dptr,a
        inc   dptr                 ; -> 0xFFFD USBIMSK, DPTR still live
        mov   a,#0x9f              ; RSTR(7)|SOF(4)|PSOF(3)|SETUP(2)|bit1|STPOW(0)
        movx  @dptr,a

        lcall _hw_master_init      ; full hardware bring-up, again
        lcall _usb_ep_dma_init

        setb  0x8c                 ; TCON.4 TR0 -- panel tick running
        setb  0xa8                 ; IE.0 EX0 -- USB engine interrupt
        setb  0xaf                 ; IE.7 EA

        mov   dptr,#0xfffc         ; USBCTL
        movx  a,@dptr
        orl   a,#0x80              ; CONN: re-attach, host re-enumerates
        movx  @dptr,a
    0001$:
        ; falls through into evt_dispatch_epilogue at 0x0564
    __endasm;
}
