// MATCH: image=rev22 addr=0x047D len=34 func=cmd7_set_clock_mode2_prog_spdif cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 7, Rev 22: go to 44.1 kHz (clock mode 2) and then tell the CS8427
 * about it. Counterpart of Rev 20's cmd7_set_cpt_mode2_progchip at 0x0480.
 *
 * Event 8 at 0x049F is the same handler with mode 3 (48 kHz) and one different
 * constant; read the two together.
 *
 * READ THE OPERANDS CAREFULLY -- this handler is the project's canonical
 * bit-vs-byte trap, and Rev 22 keeps it exactly. `JNB 0x2C` at 0x0482 tests
 * BIT address 0x2C, which is IRAM 0x25.4 (f_spdif). `MOV 0x2C,#0x23` at 0x048E
 * writes IRAM BYTE 0x2C, an unrelated location. IRAM bytes 0x2C/0x2D are the
 * register/value pair the CS8427 helpers pick up (cs8427_write_reg04_val41 at
 * 0x0567, cs8427_write_shadowed at 0x0575, stage_ctrl_pair_12_00 at 0x0FFA and
 * the shared write tail at 0x0509 all traffic in them); they are Keil
 * overlay-allocated locals of the dispatcher, which is why the same addresses
 * also serve as a settle-delay counter inside audio_clock_set_mode.
 *
 * THE BRANCH. If f_spdif is set -- S/PDIF chosen as the source, so the CS8427
 * is recovering clock from the incoming stream rather than transmitting at our
 * rate -- the rate is not programmed at all. Instead register 4 = 0x41 is
 * written (cs8427_write_reg04_val41) and register 0x12 = 0x00 is staged
 * (stage_ctrl_pair_12_00) for the shared tail to emit. Only when f_spdif is
 * clear does the rate reach the chip, as register 0x23 = 0x00 here (0x40 in
 * the 48 kHz twin), followed by register 0x24 = 0x80 which the tail emits.
 *
 * That pairing -- 0x00 for 44.1 kHz, 0x40 for 48 kHz, same register, same
 * trailing 0x24 = 0x80 -- is what identifies register 0x23 as carrying the
 * transmitted sample rate. It is an inference from the two handlers against
 * each other, not from a datasheet: no CS8427 datasheet is in this repo, and
 * the meanings of control-port registers 0x12, 0x23 and 0x24 are NOT verified
 * here. What is verified is which register gets which byte.
 *
 * REV 20 -> REV 22 DELTA: BEHAVIOUR IDENTICAL, HELPERS RE-CUT.
 * Rev 20's single helper at 0x0568 did register 4 = 0x41 AND register
 * 0x12 = 0x00; Rev 22 splits it into cs8427_write_reg04_val41 (0x0567, the
 * first write in full) plus stage_ctrl_pair_12_00 (0x0FFA, which only stages
 * 0x12/0x00 into IRAM 0x2C/0x2D), with the shared tail at 0x0509 doing the
 * second write. Likewise Rev 20's 0x0582 helper wrote the caller's pair and
 * then register 0x24 = 0x80 itself; Rev 22 calls cs8427_write_shadowed (0x0575)
 * for the pair, stages 0x24/0x80 in line at 0x0497, and lets the tail write it.
 * Net effect on the wire: the same four/two CS8427 writes in the same order.
 * The rate constants are unchanged (0x00 at 0x0491-0x0492 here, 0x40 at
 * 0x04B2-0x04B4 in event 8), and the mode number is unchanged.
 * Size: 26 bytes in Rev 20, 34 here -- the staging moved out of the helpers
 * and into the case bodies.
 *
 * NAKED: the clock mode is Keil's R7 register argument, and both exits are the
 * dispatcher switch's `break` into a merged tail. */
void cmd7_set_clock_mode2_prog_spdif(void) __naked {
    __asm
        .globl _audio_clock_set_mode
        .globl _cs8427_write_reg04_val41
        .globl _cs8427_write_shadowed
        .globl _stage_ctrl_pair_12_00
        .globl _evt_tail_write_ctrl_pair

        mov   r7,#0x02             ; clock mode 2 = 44100 Hz
        lcall _audio_clock_set_mode
        jnb   0x2c,00001$          ; BIT 0x2C = IRAM 0x25.4 = f_spdif
        lcall _cs8427_write_reg04_val41   ; reg 4 = 0x41, written now
        lcall _stage_ctrl_pair_12_00      ; stage reg 0x12 = 0x00
        ljmp  _evt_tail_write_ctrl_pair   ; 0x0509: write it, clear the event
    00001$:
        mov   0x2c,#0x23           ; BYTE 0x2C = register number 0x23
        clr   a
        mov   0x2d,a               ; BYTE 0x2D = value 0x00  (44.1 kHz)
        lcall _cs8427_write_shadowed
        mov   0x2c,#0x24           ; BYTE 0x2C = register number 0x24
        mov   0x2d,#0x80           ; BYTE 0x2D = value 0x80
        /* `sjmp _evt_tail_write_ctrl_pair`, self-relative because sdas cannot
         * short-jump to an external symbol. `.` is area-relative, so the
         * emitted displacement is the constant 0x6A at assembly time and
         * survives relocation: 0x049D + 2 + 0x6A = 0x0509. */
        sjmp  . + (0x0509 - 0x049d)   ; -> 0x0509
    __endasm;
}
