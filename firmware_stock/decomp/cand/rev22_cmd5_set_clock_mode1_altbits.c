// MATCH: image=rev22 addr=0x0469 len=15 func=cmd5_set_clock_mode1_altbits cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 5, Rev 22: select S/PDIF -- set the S/PDIF flag, clear the derived
 * panel bit, push both panel shift-register chains, and put the audio clock in
 * mode 1. Counterpart of Rev 20's cmd5_variantB_set_mode1 at 0x0466.
 *
 * The exact mirror of event 4 at 0x045A, which clears bit 0x2C, sets bit 0x16
 * and re-applies the current mode from IRAM byte 0x08. Read
 * rev22_cmd4_reapply_current_clock_mode.c first: it explains why these two
 * bits are panel shift-register bits rather than plain flags, and why the two
 * commits follow them. For bit 0x16 the owning account is
 * cand/shiftreg8_commit.c -- bit 6 of the chain-A latch byte, written by three
 * paths and read by none, nominally !(f_spdif | f_force). The `CLR 0x16` here
 * is that derived value written out directly rather than recomputed, because
 * this command has just set f_spdif.
 *
 * The one asymmetry is the mode. Event 4 re-applies whatever is current; event
 * 5 hard-codes 1, which audio_clock_set_mode implements as ACGCTL = 0x0D and
 * IRAM byte 0x08 = 1 -- the internal sample clock stops. That is consistent
 * with handing timing over to the S/PDIF receiver, but the firmware does not
 * go to mode 5 (the externally clocked mode) here; something else would have
 * to request that, and I have not traced what does. Stated as what the bytes
 * do and no further.
 *
 * REV 20 -> REV 22 DELTA: BEHAVIOUR IDENTICAL, TAIL SHARED.
 * Identical operations and identical bit addresses. Relocated helper targets
 * (0x0E56/0x0EFC against Rev 20's 0x0E62/0x0F0C) and the shared clock-apply
 * tail at 0x0512 replacing Rev 20's inline `LCALL / LJMP epilogue`, so 18
 * bytes become 15.
 *
 * NAKED for the R7 register argument and the switch-`break` exit; see
 * rev22_cmd4_reapply_current_clock_mode.c. */
void cmd5_set_clock_mode1_altbits(void) __naked {
    __asm
        .globl _shiftreg_out16_p1
        .globl _shiftreg_out8_p1hi
        .globl _evt_tail_apply_clock_mode

        setb  0x2c                 ; BIT 0x2C = IRAM 0x25.4 = f_spdif -> S/PDIF
        clr   0x16                 ; BIT 0x16 = IRAM 0x22.6 -- see shiftreg8_commit.c
        lcall _shiftreg_out16_p1   ; chain B (IRAM 0x23, 0x25)
        lcall _shiftreg_out8_p1hi  ; chain A (IRAM 0x22)
        mov   r7,#0x01             ; clock mode 1 = idle, no internal sample clock
        ljmp  _evt_tail_apply_clock_mode   ; 0x0512: apply it, clear the event
    __endasm;
}
