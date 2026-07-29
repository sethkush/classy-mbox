// MATCH: image=rev20 addr=0x04B4 len=8 func=cmd9_set_cpt_mode4 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 9: audio clock mode 4. Dispatched from event_jump_table entry 8
 * (rev20 0x0318 LJMP 0x04B4, rev22 0x0324 LJMP 0x04C0).
 *
 * Mode 4 is the one audio_clock_mode_apply does no per-mode setup for. Its
 * dispatch chain tests for 2, 3, 5 and 1 in that order (rev20 0x073C..0x074B
 * in the routine at 0x0728; rev22 0x0723..0x0732 in the routine at 0x070F --
 * byte-identical chains, relocated) and mode 4 matches none of them, so
 * control falls straight to the common tail (rev20 0x07C5, rev22 0x07A6).
 * Note the consequence: unlike every other mode, mode 4 does NOT
 * update the current-mode byte at IRAM 0x08, so the class GET_CUR handler goes
 * on reporting whatever rate was set before. The observable effect is only the
 * tail -- re-clock the shift registers, ACGCTL |= 0xC0, re-arm the endpoint
 * counters, and the ~4000-iteration settle delay.
 *
 * NAKED for the same reason as cmd6: the mode is Keil's R7 register argument.
 * See cmd6_set_cpt_mode1.c for the family.
 *
 * REV 22 CROSS-CHECK: rev22 0x04C0 is four bytes, `MOV R7,#0x04; SJMP 0x0512`
 * (7f 04 80 4e) -- Rev 22 hoisted the LCALL/epilogue into the shared tail at
 * 0x0512 that events 4, 5, 6/12, 9 and 10 all reach. Mode 4 is unchanged. */
void cmd9_set_cpt_mode4(void) __naked {
    __asm
        .globl _audio_clock_mode_apply
        .globl _evt_dispatch_epilogue
        mov   r7,#0x04
        lcall _audio_clock_mode_apply
        ljmp  _evt_dispatch_epilogue
    __endasm;
}
