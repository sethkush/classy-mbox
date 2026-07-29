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
 * "RECOVERING CLOCK FROM THE INCOMING STREAM" USED TO BE THE NARRATIVE GLOSS
 * ON THAT BRANCH; the register map now says it outright. Register 4 is
 * CLOCKSOURCE (ALSA's name), and its low two bits are RXD, the recovered
 * input clock source. Bring-up leaves the part at 0x40 = RUN | RXD 00, which
 * ALSA names RXDILRCK, "256*Fsi from ILRCK pin" -- clocked by the TAS1020B.
 * This branch writes 0x41: the same RUN bit with RXD = 01 = RXDAES3INPUT,
 * "256*Fsi from AES3 input". One bit, and it is the S/PDIF clock-slaving
 * switch. The value is identical in both images and both handlers: rev20
 * 0x0568 and rev22 0x0567 both read 75 2c 04 75 2d 41 (stage register 4,
 * value 0x41), and the earlier copy at rev20 0x04D1 / rev22 0x04D5 is the
 * same pair in the self-test path. See task #145.
 *
 * That pairing -- 0x00 for 44.1 kHz, 0x40 for 48 kHz, same register, same
 * trailing 0x24 = 0x80 -- was originally the ONLY evidence that register 0x23
 * carries the transmitted sample rate, and this comment used to call the
 * reading an inference from the two handlers against each other, with the
 * meanings of registers 0x12, 0x23 and 0x24 explicitly NOT verified.
 *
 * ============== THE REGISTER MAP NOW SUPPORTS IT ======================
 *
 * reference/cs8427/alsa_cs8427.h is the CS8427 register map as ALSA's named
 * constants. It is a SECONDARY source -- ALSA's header, not Cirrus's
 * datasheet -- so what follows is stated as what ALSA's header names things.
 *
 * ALSA names register 0x12 CSDATABUF, the channel-status data buffer control,
 * and register 0x20 CORU_DATABUF, marked "24 byte buffer area". A 24-byte
 * area based at 0x20 spans 0x20..0x37, so the buffer's byte N is at register
 * 0x20 + N:
 *
 *     register 0x23 = 0x20 + 3  ->  channel-status byte 3
 *     register 0x24 = 0x20 + 4  ->  channel-status byte 4
 *
 * (It is the CHANNEL-STATUS buffer and not the user-data buffer because
 * CSDATABUF bit 5 is BSEL, "0 = CS data, 1 = U data", and every write of
 * register 0x12 in either image writes 0x00: rev20 0x0502 and 0x0575, rev22
 * 0x0FFA, all three the byte string 75 2c 12 e4 f5 2d -- stage register 0x12,
 * value zero. That same 0x00 also means CBMR/DETCI/EFTCI = 0, CAM = 0
 * (one-byte control-port access) and CHS = 0 (channel A).)
 *
 * In AES3/S-PDIF consumer channel status, byte 3 is the byte that carries the
 * sampling-frequency field. So these two handlers are writing the outgoing
 * sample rate into the transmitted channel status, which is what the old
 * inference said -- it now rests on the register map instead of on the two
 * handlers' names.
 *
 * THE VALUES LINE UP WITH THE CONSUMER FREQUENCY CODES, and pin the bit
 * order while they are at it. The consumer sampling-frequency codes are 0000
 * for 44.1 kHz and 0100 for 48 kHz, written first-transmitted-bit-first. The
 * firmware writes 0x00 and 0x40. 0x40 is 0100 with the first transmitted bit
 * sitting at the MSB of the byte; under the opposite packing (ALSA's own
 * IEC958 byte convention, first bit at the LSB) 48 kHz would be 0x02, and
 * 0x40 would instead be a reserved bit with the frequency field left at 44.1
 * -- i.e. the two handlers would be writing the SAME rate, which they plainly
 * are not. HEDGE THAT SURVIVES: the IEC 60958 consumer channel-status layout
 * is not an artefact in this repo; alsa_cs8427.h covers the part's REGISTERS,
 * not the content of the channel-status block. The register numbers and the
 * buffer geometry are decoded; the field layout inside byte 3 is quoted from
 * the standard from outside this repo.
 *
 * REGISTER 0x24 = 0x80 (channel-status byte 4) under that same packing has
 * only its first bit set, which in the consumer layout is the "maximum audio
 * sample word length = 24 bits" bit, with the word-length field itself left
 * at zero ("not indicated"). That is consistent with the CS8427 having been
 * brought up as 24-bit on both serial ports (registers 5 and 6 = 0x05, see
 * audio_path_reconfig_ext_chips.c), but it is a reading of the same external
 * layout and carries the same hedge. It is the same 0x80 in both handlers and
 * in both images (rev20 0x0589, rev22 0x0497 and 0x04B8, all 75 2c 24 75 2d
 * 80), so it is not rate-dependent.
 *
 * REV 22 CROSS-CHECK: cmd7_set_clock_mode2_prog_spdif at rev22 0x047D is the
 * same handler refactored, not changed. Rev 20's 0x0568 helper (register 4 =
 * 0x41 THEN register 0x12 = 0x00) is split in Rev 22 into
 * cs8427_write_reg04_val41 at 0x0567 plus stage_ctrl_pair_12_00 at 0x0FFA,
 * which only stages the 0x12/0x00 pair into IRAM 0x2C/0x2D; a shared tail at
 * rev22 0x0509 then does the actual write for both arms. The f_spdif-clear arm
 * likewise stages register 0x24 = 0x80 (rev22 0x0497) and lets that tail emit
 * it, where Rev 20 buries it in the helper at 0x0582. The rate constants are
 * unchanged: 0x00 to register 0x23 for 44.1 kHz (rev22 0x048E..0x0493 --
 * MOV 0x2C,#0x23 / CLR A / MOV 0x2D,A), 0x40 for 48 kHz (rev22
 * 0x04AF..0x04B4).
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
        lcall _serial_ctl_write_04_41_then_12_00   ; reg 4 = 0x41: CLOCKSOURCE
                                   ; RXD = AES3 input, i.e. slave to the
                                   ; incoming S/PDIF. reg 0x12 = 0: CSDATABUF,
                                   ; CS data, channel A, one-byte access.
        ljmp  _evt_dispatch_epilogue
    00001$:
        mov   0x2c,#0x23           ; BYTE 0x2C = register number 0x23 =
                                   ; CORU_DATABUF + 3 = channel-status byte 3
        clr   a
        mov   0x2d,a               ; BYTE 0x2D = value 0x00 = consumer
                                   ; sample-frequency code 0000 = 44.1 kHz
        lcall _serial_ctl_write_caller_pair_then_24_80  ; ...then reg 0x24 =
                                   ; 0x80: channel-status byte 4, 24-bit max
                                   ; word length
        ljmp  _evt_dispatch_epilogue
    __endasm;
}
