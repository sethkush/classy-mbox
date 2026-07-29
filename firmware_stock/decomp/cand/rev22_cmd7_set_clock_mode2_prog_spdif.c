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
 * trailing 0x24 = 0x80 -- was once the only evidence that register 0x23
 * carries the transmitted sample rate, and this comment used to record the
 * meanings of registers 0x12, 0x23 and 0x24 as NOT verified.
 *
 * ============== THE REGISTER MAP NOW SUPPORTS IT ======================
 *
 * From reference/cs8427/alsa_cs8427.h, a SECONDARY source (ALSA's named
 * constants for the CS8427, not Cirrus's datasheet):
 *
 *   - Register 0x12 is CSDATABUF, the channel-status data buffer CONTROL
 *     register. Register 0x20 is CORU_DATABUF, marked "24 byte buffer area",
 *     so the buffer occupies 0x20..0x37 and its byte N sits at 0x20 + N:
 *         register 0x23 = 0x20 + 3 -> channel-status byte 3
 *         register 0x24 = 0x20 + 4 -> channel-status byte 4
 *   - It is channel status rather than user data because CSDATABUF bit 5 is
 *     BSEL, "0 = CS data, 1 = U data", and every write of register 0x12 in
 *     either image is 0x00 -- rev22 0x0FFA and rev20 0x0502 and 0x0575 all
 *     read 75 2c 12 e4 f5 2d. The same 0x00 also means CAM = 0 (one-byte
 *     control-port access) and CHS = 0 (channel A).
 *   - Byte 3 is the byte that carries the sampling-frequency field in
 *     AES3/S-PDIF consumer channel status. These handlers are therefore
 *     writing the outgoing sample rate into the transmitted channel status --
 *     what the old inference said, now resting on the register map.
 *   - 0x00 and 0x40 are the consumer sampling-frequency codes for 44.1 kHz
 *     (0000) and 48 kHz (0100) with the first transmitted bit at the MSB of
 *     the byte. Under the opposite packing 0x40 would leave the frequency
 *     field reading 44.1 kHz in BOTH handlers, which it plainly is not.
 *   - Register 0x24 = 0x80 is channel-status byte 4 with only its first bit
 *     set: "maximum audio sample word length = 24 bits", consistent with the
 *     serial ports being brought up 24-bit (registers 5 and 6 = 0x05). It is
 *     the same 0x80 in both handlers and both images -- rev22 0x0497 and
 *     0x04B8, rev20 0x0589, all 75 2c 24 75 2d 80 -- so it is not
 *     rate-dependent.
 *
 * HEDGE THAT SURVIVES: the IEC 60958 consumer channel-status layout is not an
 * artefact in this repo. alsa_cs8427.h decodes the part's REGISTERS and the
 * buffer geometry; the field layout inside channel-status bytes 3 and 4 is
 * quoted from the standard from outside this repo.
 *
 * AND THE OTHER ARM DECODES TOO. Register 4 is CLOCKSOURCE, whose low two
 * bits are RXD, the recovered input clock source. Bring-up leaves it at
 * 0x40 = RUN with RXD = 00 = RXDILRCK, "256*Fsi from ILRCK pin" -- clocked by
 * the TAS1020B. cs8427_write_reg04_val41 writes 0x41: same RUN bit, RXD = 01 =
 * RXDAES3INPUT, "256*Fsi from AES3 input". That one bit is the S/PDIF
 * clock-slaving switch, which is why this arm does not program a rate at all.
 * rev22 0x0567 and rev20 0x0568 both read 75 2c 04 75 2d 41. See task #145.
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
 * The rate constants are unchanged (0x00 at 0x0491..0x0493 here, CLR A /
 * MOV 0x2D,A; 0x40 at 0x04B2..0x04B4 in event 8), and the mode number is
 * unchanged.
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
        lcall _cs8427_write_reg04_val41   ; reg 4 = 0x41: CLOCKSOURCE with
                                   ; RXD = AES3 input -- slave to the incoming
                                   ; S/PDIF instead of the ILRCK pin
        lcall _stage_ctrl_pair_12_00      ; stage reg 0x12 = 0x00: CSDATABUF,
                                   ; CS data, channel A, one-byte access
        ljmp  _evt_tail_write_ctrl_pair   ; 0x0509: write it, clear the event
    00001$:
        mov   0x2c,#0x23           ; BYTE 0x2C = register number 0x23 =
                                   ; CORU_DATABUF + 3 = channel-status byte 3
        clr   a
        mov   0x2d,a               ; BYTE 0x2D = value 0x00 = consumer
                                   ; sample-frequency code 0000 = 44.1 kHz
        lcall _cs8427_write_shadowed
        mov   0x2c,#0x24           ; BYTE 0x2C = register 0x24 = channel-status
                                   ; byte 4
        mov   0x2d,#0x80           ; BYTE 0x2D = value 0x80 = 24-bit maximum
                                   ; word length
        /* `sjmp _evt_tail_write_ctrl_pair`, self-relative because sdas cannot
         * short-jump to an external symbol. `.` is area-relative, so the
         * emitted displacement is the constant 0x6A at assembly time and
         * survives relocation: 0x049D + 2 + 0x6A = 0x0509. */
        sjmp  . + (0x0509 - 0x049d)   ; -> 0x0509
    __endasm;
}
