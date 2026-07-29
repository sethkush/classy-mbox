// MATCH: image=rev20 addr=0x049A len=26 func=cmd8_set_cpt_mode3_progchip cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 8: go to 48 kHz (clock mode 3) and then tell the CS8427 about it.
 *
 * The twin of event 7 at 0x0480 -- same shape, same branch, same helpers. Read
 * cmd7_set_cpt_mode2_progchip.c for the bit-vs-byte trap on 0x2C and for what
 * is and is not verified about the CS8427 register numbers.
 *
 * Two differences from event 7, and only two:
 *   - the mode is 3 (48 kHz) instead of 2 (44.1 kHz);
 *   - the value written to register 0x23 is 0x40 instead of 0x00.
 *
 * Keil also encoded the constant differently: event 7's zero is CLR A +
 * MOV 0x2D,A (0x0491) while the 0x40 here is a direct MOV 0x2D,#0x40
 * (0x04AB). Both are three bytes, so this is Keil's habitual "produce zero in
 * A" rather than a size win -- the same idiom keil.peep encodes for SDCC.
 *
 * REV 22 CROSS-CHECK: cmd8_set_clock_mode3_prog_spdif at rev22 0x049F. Same
 * mode 3, same register 0x23 = 0x40 (rev22 0x04AF..0x04B2), with Rev 22's
 * shared tail at 0x0509 doing the register 0x24 = 0x80 write that Rev 20
 * delegates to the helper at 0x0582.
 *
 * NAKED: the clock mode is Keil's R7 register argument. See
 * cmd6_set_cpt_mode1.c. */
void cmd8_set_cpt_mode3_progchip(void) __naked {
    __asm
        .globl _audio_clock_mode_apply
        .globl _serial_ctl_write_04_41_then_12_00
        .globl _serial_ctl_write_caller_pair_then_24_80
        .globl _evt_dispatch_epilogue

        mov   r7,#0x03             ; clock mode 3 = 48000 Hz
        lcall _audio_clock_mode_apply
        jnb   0x2c,00001$          ; BIT 0x2C = IRAM 0x25.4 = f_spdif
        lcall _serial_ctl_write_04_41_then_12_00   ; reg 4 = 0x41, reg 0x12 = 0
        ljmp  _evt_dispatch_epilogue
    00001$:
        mov   0x2c,#0x23           ; BYTE 0x2C = register number 0x23
        mov   0x2d,#0x40           ; BYTE 0x2D = value 0x40  (48 kHz)
        lcall _serial_ctl_write_caller_pair_then_24_80  ; ...then reg 0x24 = 0x80
        ljmp  _evt_dispatch_epilogue
    __endasm;
}
