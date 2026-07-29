// MATCH: image=rev20 addr=0x0466 len=18 func=cmd5_variantB_set_mode1 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 5: select S/PDIF -- set the S/PDIF flag, clear the derived-clock bit,
 * push both panel shift-register chains, and put the audio clock in mode 1.
 *
 * The exact mirror of event 4 at 0x0454, which clears 0x2C, sets 0x16 and
 * re-applies the current mode from IRAM 0x08. Read cmd4_variantA_reapply_mode.c
 * first: it explains why these two bits are panel shift-register bits rather
 * than plain flags, and why the two commits follow them. For bit 0x16
 * specifically the owning account is in shiftreg8_commit.c -- it is bit 6 of
 * the chain-A latch byte, written by three different paths and read by none,
 * nominally !(f_spdif | f_force). The `CLR 0x16` here is that derived value
 * written out directly rather than recomputed, because this command has just
 * set f_spdif. "p_derived" is a label of convenience, not an established
 * meaning.
 *
 * The one asymmetry is the mode. Event 4 re-applies whatever is current;
 * event 5 hard-codes 1, which audio_clock_mode_apply implements as ACGCTL =
 * 0x0D and IRAM 0x08 = 1 (0x074D..0x0759) -- the internal sample clock stops.
 * That is consistent with handing timing over to the S/PDIF receiver, but the
 * firmware does not go to mode 5 (the externally-clocked mode that halves
 * CPTRXCNF4's divider) here; something else has to request that. I have not
 * traced what does, so this is stated as what the bytes do and no further.
 *
 * REV 22 CROSS-CHECK: cmd5_set_clock_mode1_altbits at rev22 0x0469, identical
 * operations and identical bit addresses, with the relocated helper targets
 * (0x0E56 / 0x0EFC) and Rev 22's shared clock-apply tail at 0x0512.
 *
 * NAKED for the R7 register argument and the LJMP exit; see
 * cmd6_set_cpt_mode1.c. */
void cmd5_variantB_set_mode1(void) __naked {
    __asm
        .globl _shiftreg16_commit
        .globl _shiftreg8_commit
        .globl _audio_clock_mode_apply
        .globl _evt_dispatch_epilogue

        setb  0x2c                 ; IRAM 0x25.4 = f_spdif  -> S/PDIF selected
        clr   0x16                 ; IRAM 0x22.6 -- see shiftreg8_commit.c
        lcall _shiftreg16_commit   ; chain B (IRAM 0x23, 0x25)
        lcall _shiftreg8_commit    ; chain A (IRAM 0x22)
        mov   r7,#0x01             ; clock mode 1 = idle, no sample clock
        lcall _audio_clock_mode_apply
        ljmp  _evt_dispatch_epilogue
    __endasm;
}
