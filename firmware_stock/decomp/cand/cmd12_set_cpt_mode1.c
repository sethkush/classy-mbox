// MATCH: image=rev20 addr=0x0511 len=7 func=cmd12_set_cpt_mode1 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 12: audio clock mode 1 (idle) -- byte-for-byte the same work as
 * event 6 at 0x0478, reached from a different jump-table slot. Two separate
 * case labels in the dispatcher's switch produce the same call, so Keil
 * emitted the body twice rather than sharing it.
 *
 * The only difference from cmd6 is the exit encoding: this case body sits
 * 0x4C bytes from the epilogue at 0x0564, inside SJMP range, so the `break`
 * is a 2-byte SJMP instead of a 3-byte LJMP. That is the direct evidence that
 * these "functions" are case bodies of one source function -- a short jump
 * only reaches a nearby target, and the target here is the epilogue of the
 * function the case body is inside.
 *
 * REV 22 CROSS-CHECK, and it settles the point: Rev 22 merged the two into one
 * body, cmd6_12_set_clock_mode1 at rev22 0x0478, whose XREFs are the two
 * jump-table slots 0x031B and 0x032D. Two case labels, one body -- which is
 * what a `case 6: case 12:` looks like after the compiler stops duplicating
 * it. Rev 22 also hoisted the `LCALL audio_clock_mode_apply` itself into a
 * tail at 0x0512 shared by five of these handlers.
 *
 * NAKED for the R7 register argument, as with the rest of the family. */
void cmd12_set_cpt_mode1(void) __naked {
    __asm
        .globl _audio_clock_mode_apply
        mov   r7,#0x01             ; clock mode 1 = idle
        lcall _audio_clock_mode_apply
        /* `sjmp _evt_dispatch_epilogue` is what this is, but sdas cannot encode
         * a short jump to an external symbol -- it emits 80 00 and leaves the
         * displacement to the linker, which is one byte short of a match. The
         * displacement is therefore written self-relative instead. `.` is the
         * address of this instruction, area-relative, so the subtraction is a
         * constant 0x4E at assembly time and survives relocation unchanged:
         * standalone it assembles to 80 4C, and linked at the stock address it
         * is still 80 4C, i.e. 0x0516 + 2 + 0x4C = 0x0564. The two constants
         * are spelt out as the stock addresses they are so the target is
         * legible, and the byte itself is compared against stock either way. */
        sjmp  . + (0x0564 - 0x0516)   ; -> evt_dispatch_epilogue
    __endasm;
}
