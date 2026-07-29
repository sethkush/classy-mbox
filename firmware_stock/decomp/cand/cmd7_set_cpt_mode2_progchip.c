// MATCH: image=rev20 addr=0x0480 len=26 func=cmd7_set_cpt_mode2_progchip cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 7: go to 44.1 kHz (clock mode 2) and then tell the CS8427 about it.
 *
 * Event 8 at 0x049A is the same function with mode 3 (48 kHz) and a different
 * constant; the two differ in exactly two bytes of payload.
 *
 * READ THE OPERANDS CAREFULLY -- this handler is the project's canonical
 * bit-vs-byte trap. `JNB 0x2C` at 0x0485 tests BIT address 0x2C, which is IRAM
 * 0x25.4 (f_spdif). `MOV 0x2C,#0x23` at 0x048E writes IRAM BYTE 0x2C, an
 * unrelated location. They print identically in a listing and they are not the
 * same storage. IRAM bytes 0x2C/0x2D are the register/value pair the serial
 * control helpers at 0x0568 and 0x0582 pick up (both do `MOV R5,0x2D` /
 * `MOV R7,0x2C` before calling cs8427_ctl_write at 0x0C45); they are Keil
 * overlay-allocated locals of the dispatcher, which is why the same addresses
 * serve as a settle-delay counter inside audio_clock_mode_apply.
 *
 * THE BRANCH. If f_spdif is set -- S/PDIF chosen as the source, so the CS8427
 * is recovering clock from the incoming stream rather than transmitting at our
 * rate -- the rate is not programmed at all; the helper at 0x0568 writes
 * register 4 = 0x41 and register 0x12 = 0x00 instead. Only when f_spdif is
 * clear does the rate reach the chip, as register 0x23 = 0x00 here (0x40 in
 * the 48 kHz twin at 0x04AB) followed by register 0x24 = 0x80, which the
 * helper at 0x0582 appends.
 *
 * That pairing -- 0x00 for 44.1 kHz, 0x40 for 48 kHz, same register, same
 * trailing 0x24 = 0x80 -- is what identifies register 0x23 as carrying the
 * transmitted sample rate. It is an inference from the two handlers against
 * each other, not from a datasheet: no CS8427 datasheet is in this repo, and
 * the meanings of control-port registers 0x12, 0x23 and 0x24 are NOT verified
 * here. What is verified is which register gets which byte.
 *
 * REV 22 CROSS-CHECK: cmd7_set_clock_mode2_prog_spdif at rev22 0x047D is the
 * same handler refactored, not changed. Rev 20's 0x0568 helper (register 4 =
 * 0x41 THEN register 0x12 = 0x00) is split in Rev 22 into
 * cs8427_write_reg04_val41 at 0x0567 plus stage_ctrl_pair_12_00 at 0x0FFA,
 * which only stages the 0x12/0x00 pair into IRAM 0x2C/0x2D; a shared tail at
 * rev22 0x0509 then does the actual write for both arms. The f_spdif-clear arm
 * likewise stages register 0x24 = 0x80 (rev22 0x0497) and lets that tail emit
 * it, where Rev 20 buries it in the helper at 0x0582. The rate constants are
 * unchanged: 0x00 to register 0x23 for 44.1 kHz (rev22 0x048E..0x0492), 0x40
 * for 48 kHz (rev22 0x04AF..0x04B2).
 *
 * NAKED: the clock mode is Keil's R7 register argument. See
 * cmd6_set_cpt_mode1.c. */
void cmd7_set_cpt_mode2_progchip(void) __naked {
    __asm
        .globl _audio_clock_mode_apply
        .globl _serial_ctl_write_04_41_then_12_00
        .globl _serial_ctl_write_caller_pair_then_24_80
        .globl _evt_dispatch_epilogue

        mov   r7,#0x02             ; clock mode 2 = 44100 Hz
        lcall _audio_clock_mode_apply
        jnb   0x2c,00001$          ; BIT 0x2C = IRAM 0x25.4 = f_spdif
        lcall _serial_ctl_write_04_41_then_12_00   ; reg 4 = 0x41, reg 0x12 = 0
        ljmp  _evt_dispatch_epilogue
    00001$:
        mov   0x2c,#0x23           ; BYTE 0x2C = register number 0x23
        clr   a
        mov   0x2d,a               ; BYTE 0x2D = value 0x00  (44.1 kHz)
        lcall _serial_ctl_write_caller_pair_then_24_80  ; ...then reg 0x24 = 0x80
        ljmp  _evt_dispatch_epilogue
    __endasm;
}
