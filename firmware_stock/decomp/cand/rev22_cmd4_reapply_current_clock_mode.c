// MATCH: image=rev22 addr=0x045A len=15 func=cmd4_reapply_current_clock_mode cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 4, Rev 22: select the internal clock source -- clear the S/PDIF flag,
 * set the derived-clock panel bit, push both panel shift-register chains, and
 * re-apply whatever clock mode is currently in force. Counterpart of Rev 20's
 * cmd4_variantA_reapply_mode at 0x0454.
 *
 * Event 5 at 0x0469 is the mirror image; the two are a matched pair and are
 * best read together.
 *
 * THE TWO BITS ARE PANEL BITS, NOT JUST FLAGS. Bit address 0x2C is IRAM 0x25.4
 * and bit address 0x16 is IRAM 0x22.6, and IRAM 0x22/0x23/0x25 are the
 * shift-register payload bytes themselves (shiftreg_out8_p1hi at 0x0EFC,
 * shiftreg_out16_p1 at 0x0E56). So "clear f_spdif, set the derived bit" and
 * "shift the chains out" are one action: the state variable and the bit that
 * drives the front-panel hardware are the same bit. That is also why the two
 * commits appear here at all, and why they run 16-bit chain first then 8-bit
 * chain -- 0x16 lives in the byte the 8-bit chain sends, so the chain carrying
 * the change is committed last.
 *
 * WHAT 0x16 MEANS IS NOT DECIDED HERE. Bit 0x16 has three writers in each
 * image and cand/shiftreg8_commit.c holds the whole account: it is bit 6 of
 * the chain-A latch byte, write-only (no instruction in either image reads
 * it), nominally carrying !(f_spdif | f_force). The `SETB 0x16` here is that
 * derived value written out directly, because this command has just cleared
 * f_spdif and so already knows the answer. cmd11_eeprom_selftest reuses the
 * same output line to report its result. Do not treat "p_derived" as a
 * meaning.
 *
 * The clock mode is re-applied, not chosen: R7 comes from IRAM BYTE 0x08, the
 * current-mode byte audio_clock_set_mode maintains. Event 5 by contrast
 * hard-codes mode 1. (Note the bit/byte doubling again: BIT 0x08 is IRAM
 * 0x21.0, an unrelated flag the dispatcher tests; `MOV R7,0x08` here reads the
 * BYTE.)
 *
 * REV 20 -> REV 22 DELTA: BEHAVIOUR IDENTICAL, TAIL SHARED.
 * The same six operations with the same two bit addresses. Rev 20 ended with
 * `MOV R7,0x08 / LCALL audio_clock_mode_apply / LJMP epilogue` (18 bytes);
 * Rev 22 ends with `MOV R7,0x08 / LJMP 0x0512` (15 bytes), where 0x0512 is the
 * merged tail `LCALL audio_clock_set_mode / SJMP epilogue` shared with events
 * 5, 6/12, 9 and 10. Helper addresses relocated: 0x0E56/0x0EFC here against
 * Rev 20's 0x0E62/0x0F0C.
 *
 * NAKED: the clock mode is Keil's R7 register argument, and the exit is the
 * switch `break` into a merged tail, which a non-naked SDCC function would
 * follow with a RET. */
void cmd4_reapply_current_clock_mode(void) __naked {
    __asm
        .globl _shiftreg_out16_p1
        .globl _shiftreg_out8_p1hi
        .globl _evt_tail_apply_clock_mode

        clr   0x2c                 ; BIT 0x2C = IRAM 0x25.4 = f_spdif -> internal
        setb  0x16                 ; BIT 0x16 = IRAM 0x22.6 -- see shiftreg8_commit.c
        lcall _shiftreg_out16_p1   ; chain B (IRAM 0x23, 0x25)
        lcall _shiftreg_out8_p1hi  ; chain A (IRAM 0x22) -- carries 0x22.6
        mov   r7,0x08              ; BYTE 0x08: the clock mode currently in force
        ljmp  _evt_tail_apply_clock_mode   ; 0x0512: apply it, clear the event
    __endasm;
}
