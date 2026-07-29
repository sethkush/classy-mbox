// MATCH: image=rev22 addr=0x0A3F len=124 func=main_loop cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void usb_deferred_action_dispatch(void);  /* rev22 0x02F3;
                                                  * rev20 0x02EE, Ghidra
                                                  * device_event_dispatch */

__data __at (0x27) unsigned char g_p31_latch; /* edge latch for the P3.1 input */
__bit  __at (0x20) f_panel_tick;   /* IRAM 0x24.0 — set by the Timer 0 ISR   */
__bit  __at (0x01) p3_prev_1;      /* IRAM 0x20.1 — P3.1 of the last scan    */

/* The whole of the foreground program. Reached by LJMP from the tail of the
 * Keil startup at rev22 0x0936 (see cand/rev22_c51_startup.c) and never
 * returns.
 *
 * Shape: bring the hardware up with interrupts off, wait out a long delay,
 * start the panel tick and attach to the bus, then loop forever servicing two
 * things -- queued device events, and the panel tick.
 *
 * Everything in the loop is polled. The interrupt handlers do almost nothing
 * themselves: Timer 0 (rev22 timer0_tick_isr 0x1016, rev20 0x101E) only
 * reloads TH0 and sets bit 0x20, and the USB engine's handler queues an event
 * code in IRAM 0x0A. The work happens here.
 *
 * REV 20 -> REV 22 DELTA: NO BEHAVIOUR CHANGE IN THIS FUNCTION. rev22 0x0A3F
 * and rev20 0x0A95 are both 124 bytes and differ in exactly TWELVE bytes,
 * every one of them the operand half of an LCALL (diffed directly from the two
 * .bin images; run offsets 0x19/0x1A, 0x1C/0x1D, 0x47, 0x4B/0x4C, 0x52/0x53,
 * 0x56, 0x66, 0x77):
 *     hw_master_init / hw_clock_codec_init    0x08CB -> 0x07EC
 *     usb_ep_dma_init                         0x0970 -> 0x0891
 *     device_event_dispatch / usb_deferred_action_dispatch
 *                                             0x02EE -> 0x02F3  (3 sites,
 *                                             only the low byte differs)
 *     p3_button_scan / p3_edge_poll_dispatch  0x0ED5 -> 0x0F31
 *     shiftreg8_commit / shiftreg_out8_p1hi   0x0F0C -> 0x0EFC
 *     shiftreg16_commit / shiftreg_out16_p1   0x0E62 -> 0x0E56
 * Every instruction, immediate, IRAM address, SFR address and branch
 * displacement is identical. Whatever Rev 22 fixed, it did not fix it here:
 * the foreground loop's structure, the 0xFFFF attach delay, the order of
 * bring-up, and the point at which USBCTL.CONN goes high are all unchanged.
 *
 * The C is cand/main.c ported verbatim -- only the header line, the callee
 * names, and the comments were touched. It matched on the first build, which
 * also confirms the two prologue/epilogue __asm blobs are the right shape for
 * rev22 and not just for rev20.
 *
 * Named main_loop (Ghidra's rev22 name) rather than main, and for a reason
 * beyond taste: SDCC gives a function called `main` special treatment and
 * makes the module reference __sdcc_gsinit_startup and __mcs51_genRAMCLEAR,
 * neither of which exists here -- rev22 0x092A is the real startup and it is
 * hand-written Keil assembly. Those unresolved globals would fail the
 * whole-image link. rev22_c51_startup.c's `LJMP _main_loop` resolves against
 * this name, so the call edge is still checked for real. */
void main_loop(void) {
    /* The prologue is straight-line IRAM/SFR programming of the kind
     * hw_clock_codec_init is written in, and for the same reason: Keil kept A live
     * across the three 0xFF stores (MOV A,#0xFF once, then MOV dir,A three
     * times), which SDCC will not do, and it wrote IRAM 0x2A with the 3-byte
     * MOV dir,#0 form rather than CLR A + MOV dir,A because A was still
     * holding 0xFF. Steering SDCC to either needs a one-off adjacency rule,
     * which the project rejects in favour of annotated assembly.
     *
     * The 16-bit delay counter is a second reason. Keil laid it out
     * big-endian -- IRAM 0x28 high, 0x29 low -- and SDCC's `unsigned int` is
     * little-endian, so no C declaration at 0x28 can produce these bytes. */
    __asm
        .globl _hw_clock_codec_init
        .globl _usb_ep_dma_init

        clr   a
        mov   0x27,a               ; P3.1 edge latch = released
        mov   a,#0xff
        mov   0x28,a               ; delay counter, HIGH byte
        mov   0x29,a               ; delay counter, LOW  byte  -> 0xFFFF
        mov   0x2a,#0x00           ; IRAM bytes 0x2A and 0x2B are written here
        mov   0x2b,#0x10           ; and never read again -- no instruction in
                                   ; either image names 0x2A/0x2B as a direct
                                   ; operand afterwards, and no bit in the
                                   ; 0x50..0x5F range they cover is touched.
                                   ; Dead state in rev20 and rev22 alike.
                                   ; (Trap: `CLR 0x2a` elsewhere is BIT 0x2A =
                                   ; IRAM 0x25.2, an unrelated flag.)

        clr   0xaf                 ; EA = 0 for the whole bring-up
        mov   dptr,#0xfffd         ; USBIMSK
        clr   a
        movx  @dptr,a              ; mask every USB interrupt source. The USB
                                   ; engine is left masked here; whatever
                                   ; enables it does so later.
        clr   0x22                 ; BIT 0x22 = IRAM 0x24.2. Cleared exactly
                                   ; once, here, in both images, and never set
                                   ; or tested anywhere. Vestigial.
        lcall _hw_clock_codec_init ; rev22 0x07EC (rev20 hw_master_init
                                   ; 0x08CB) — clocks, codec port, panel
        lcall _usb_ep_dma_init     ; rev22 0x0891 (rev20 0x0970) — endpoint
                                   ; and DMA configuration

        ; Attach delay, run with interrupts still off. Counts 0x28:0x29 down
        ; from 0xFFFF to 0 -- roughly 65535 iterations of a 5-instruction
        ; loop. The test is Keil's "x >= 1" idiom: SETB C, then SUBB #0
        ; through both bytes, so the borrow out of the high byte is the
        ; word reaching zero.
    0001$:
        setb  c
        mov   a,0x29
        subb  a,#0x00
        mov   a,0x28
        subb  a,#0x00
        jc    0002$
        mov   a,0x29               ; 16-bit decrement: low byte first, and its
        dec   0x29                 ; pre-decrement value decides whether the
        jnz   0001$                ; high byte has to borrow
        dec   0x28
        sjmp  0001$
    0002$:

        setb  0x8c                 ; TCON.4 = TR0: start Timer 0, the panel tick
        setb  0xaf                 ; EA = 1
        mov   dptr,#0xfffc         ; USBCTL
        movx  a,@dptr
        orl   a,#0x80              ; CONN = 1: pull D+ up and let the host
        movx  @dptr,a              ; enumerate, now that everything is ready
    __endasm;

    for (;;) {
        /* No tick pending: drain the event queue instead. IRAM 0x0A holds one
         * pending event code, 1..14; device_event_dispatch (0x02EE) indexes
         * usb_deferred_action_dispatch (rev22 0x02F3) indexes the jump
         * table at rev22 0x030C with it. */
        if (!f_panel_tick) {
            if (g_event != 0) usb_deferred_action_dispatch();
            continue;
        }

        /* Panel tick. p3_edge_poll_dispatch (rev22 0x0F31) samples P3, compares it against the
         * previous sample kept in IRAM byte 0x20, calls a per-button handler
         * on each edge it cares about, stores the new sample back into 0x20,
         * and returns a "something changed" flag. Only if something changed
         * are the two shift-register chains re-clocked.
         *
         * In assembly because of the return convention: Keil returns a char
         * in R7 and stock reads it with the 1-byte MOV A,R7. SDCC returns in
         * DPL, so the same C costs 2 bytes here. */
        __asm
            .globl _p3_edge_poll_dispatch
            .globl _shiftreg_out8_p1hi
            .globl _shiftreg_out16_p1
            lcall _p3_edge_poll_dispatch ; rev22 0x0F31
            mov   a,r7
            jnb   0xe0,0003$          ; ACC.0 = "panel state changed"
            lcall _shiftreg_out8_p1hi ; rev22 0x0EFC
            lcall _shiftreg_out16_p1  ; rev22 0x0E56
        0003$:
        __endasm;

        /* P3.1 is handled here rather than inside p3_edge_poll_dispatch, and unlike
         * the three inputs that function deals with (P3.3/4/5, which fire on
         * a rising edge) this one fires on BOTH edges, with IRAM 0x27 as the
         * latch that turns a level into a pair of edges.
         *
         * p3_prev_1 is bit 1 of IRAM byte 0x20, the sample
         * p3_edge_poll_dispatch just stored, so the test below reads the fresh
         * value. P3 is idled high by hw_clock_codec_init (MOV P3,#0xFF at
         * rev22 0x07FD), so low is asserted.
         *
         * Events 11 and 12 reach the jump table at rev22 0x030C+3*(n-1):
         * 0x032A -> LJMP 0x04C8 (cmd11_eeprom_selftest) and 0x032D -> LJMP
         * 0x0478 (cmd6_12_set_clock_mode1). That second entry IS a rev20 ->
         * rev22 delta, though it lives in the table rather than here: rev20's
         * entry 12 at 0x0321 jumped to 0x0511, a seven-byte function
         * (MOV R7,#1 / LCALL 0x0728 / SJMP 0x0564) that is a duplicate of
         * rev20's entry-6 handler at 0x0478 (MOV R7,#1 / LCALL 0x0728 /
         * LJMP 0x0564) -- same operation, different tail encoding. Rev 22
         * dropped the duplicate and points both entries at 0x0478. Same
         * behaviour, seven bytes saved. What the handler does is out of scope
         * here; this function only turns the pin into the two event codes. */
        if (!p3_prev_1 && g_p31_latch == 0) {
            g_p31_latch = 1;
            g_event = 11;
            usb_deferred_action_dispatch();
        }
        if (p3_prev_1 && g_p31_latch == 1) {
            g_p31_latch = 0;
            g_event = 12;
            usb_deferred_action_dispatch();
        }
        f_panel_tick = 0;
    }
}
