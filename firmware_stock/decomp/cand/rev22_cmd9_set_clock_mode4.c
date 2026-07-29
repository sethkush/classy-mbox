// MATCH: image=rev22 addr=0x04C0 len=4 func=cmd9_set_clock_mode4 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 9, Rev 22: audio clock mode 4. Dispatched from jump-table entry 8
 * (rev22 0x0324 LJMP 0x04C0; rev20 0x0318 LJMP 0x04B4).
 *
 * Mode 4 is the one audio_clock_set_mode does no per-mode setup for. Its
 * dispatch chain tests for 2, 3, 5 and 1 in that order (rev22 0x0723..0x0732
 * inside the routine at 0x070F; rev20 0x073C..0x074B inside the routine at
 * 0x0728 -- byte-identical chains, relocated) and mode 4 matches none of them,
 * so control falls straight to the common tail (rev22 0x07A6, rev20 0x07C5).
 * The consequence is worth stating: unlike every other mode, mode 4 does NOT
 * update the current-mode byte at IRAM 0x08, so the class GET_CUR handler goes
 * on reporting whatever rate was set before. The observable effect is only the
 * tail -- re-clock the shift registers, ACGCTL |= 0xC0, re-arm the endpoint
 * counters, and the settle delay.
 *
 * NAKED for the R7 register argument; see rev22_cmd6_12_set_clock_mode1.c.
 *
 * REV 20 -> REV 22 DELTA: BEHAVIOUR IDENTICAL, TAIL SHARED. Rev 20 was 8 bytes
 * (`MOV R7,#4 / LCALL audio_clock_mode_apply / LJMP epilogue`); Rev 22 is 4
 * (`MOV R7,#4 / SJMP 0x0512`), the LCALL having moved into the merged tail.
 * Mode 4 is unchanged. */
void cmd9_set_clock_mode4(void) __naked {
    __asm
        mov   r7,#0x04             ; clock mode 4 -- no per-mode setup at all
        /* `sjmp _evt_tail_apply_clock_mode` (0x0512), written self-relative
         * because sdas cannot short-jump to an external symbol. `.` is
         * area-relative, so the displacement is the constant 0x4E at assembly
         * time and survives relocation: 0x04C2 + 2 + 0x4E = 0x0512. */
        sjmp  . + (0x0512 - 0x04c2)
    __endasm;
}
