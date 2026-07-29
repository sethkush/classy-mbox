// MATCH: image=rev20 addr=0x0454 len=18 func=cmd4_variantA_reapply_mode cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 4: select the internal clock source -- clear the S/PDIF flag, light
 * the derived-clock bit, push both panel shift-register chains, and re-apply
 * whatever clock mode is currently in force.
 *
 * Event 5 (0x0466) is the mirror image of this one; the two are a matched
 * pair and are best read together.
 *
 * THE TWO BITS ARE PANEL BITS, NOT JUST FLAGS. Bit address 0x2C is IRAM
 * 0x25.4 and bit address 0x16 is IRAM 0x22.6, and IRAM 0x22/0x23/0x25 are the
 * shift-register payload bytes themselves (see mbox.h and shiftreg8_commit at
 * 0x0F0C / shiftreg16_commit at 0x0E62). So "clear f_spdif, set p_derived"
 * and "shift the chains out" are one action: the state variable and the bit
 * that drives the front-panel hardware are the same bit. That is also why the
 * two commits appear here at all, and why they run in the order 16-bit chain
 * then 8-bit chain -- 0x16 lives in the byte the 8-bit chain sends, so the
 * chain carrying the change is committed last.
 *
 * WHAT 0x16 MEANS IS NOT DECIDED HERE. Bit 0x16 has three writers across the
 * image and shiftreg8_commit.c holds the whole account of it: it is bit 6 of
 * the chain-A latch byte, write-only (no instruction in either image reads
 * it), nominally carrying !(f_spdif | f_force) as the two button state
 * machines recompute it at rev20 0x0E52-0x0E60 and 0x0EC5-0x0ED3. The `SETB
 * 0x16` here is not an independent flag being raised -- it is that derived
 * value written out directly, because this command has just cleared f_spdif
 * and so already knows the answer. cmd11_eeprom_selftest reuses the same
 * output line to report its result. Read shiftreg8_commit.c before treating
 * "p_derived" as a name for anything.
 *
 * The clock mode is re-applied, not chosen: R7 comes from IRAM 0x08, the
 * current-mode byte audio_clock_mode_apply maintains. Event 5 by contrast
 * hard-codes mode 1.
 *
 * REV 22 CROSS-CHECK: cmd4_reapply_current_clock_mode at rev22 0x045A is the
 * same six operations with relocated targets (0x0E56 / 0x0EFC / and a shared
 * `LCALL audio_clock_mode_apply; SJMP epilogue` tail at rev22 0x0512 in place
 * of the direct call here). Same bit addresses 0x2C and 0x16 in both.
 *
 * NAKED: the clock mode is Keil's R7 register argument, and the exit is the
 * switch `break` (LJMP), which a non-naked SDCC function would follow with a
 * RET. See cmd6_set_cpt_mode1.c. */
void cmd4_variantA_reapply_mode(void) __naked {
    __asm
        .globl _shiftreg16_commit
        .globl _shiftreg8_commit
        .globl _audio_clock_mode_apply
        .globl _evt_dispatch_epilogue

        clr   0x2c                 ; IRAM 0x25.4 = f_spdif  -> analog/internal
        setb  0x16                 ; IRAM 0x22.6 -- see shiftreg8_commit.c
        lcall _shiftreg16_commit   ; chain B (IRAM 0x23, 0x25)
        lcall _shiftreg8_commit    ; chain A (IRAM 0x22) -- carries 0x22.6
        mov   r7,0x08              ; current clock mode, as recorded at IRAM 0x08
        lcall _audio_clock_mode_apply
        ljmp  _evt_dispatch_epilogue
    __endasm;
}
