// MATCH: image=rev22 addr=0x049F len=33 func=cmd8_set_clock_mode3_prog_spdif cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 8, Rev 22: go to 48 kHz (clock mode 3) and then tell the CS8427 about
 * it. Counterpart of Rev 20's cmd8_set_cpt_mode3_progchip at 0x049A.
 *
 * The twin of event 7 at 0x047D -- same shape, same f_spdif branch, same
 * helpers, same shared write tail at 0x0509. Read
 * rev22_cmd7_set_clock_mode2_prog_spdif.c first: it holds the bit-vs-byte trap
 * on 0x2C and the full decode of the CS8427 registers these two handlers
 * write, against reference/cs8427/alsa_cs8427.h (a SECONDARY source: ALSA's
 * header, not Cirrus's datasheet). In brief:
 *
 *   - ALSA puts CORU_DATABUF at register 0x20 and calls it a 24-byte buffer
 *     area, i.e. 0x20..0x37, so register 0x23 is channel-status BYTE 3 and
 *     register 0x24 is channel-status BYTE 4;
 *   - byte 3 is where AES3/S-PDIF consumer channel status carries the
 *     sampling-frequency field, so "these handlers write the transmitted
 *     sample rate" now follows from the register map rather than from
 *     comparing the two handlers with each other;
 *   - the f_spdif arm's register 4 = 0x41 is CLOCKSOURCE with RXD = AES3
 *     input -- slave the clock to the incoming S/PDIF rather than to the
 *     ILRCK pin that bring-up selects with 0x40.
 *
 * Two differences from event 7, and only two:
 *   - the mode is 3 (48 kHz) instead of 2 (44.1 kHz);
 *   - the value written to register 0x23 is 0x40 instead of 0x00, and 0x40 is
 *     the consumer sampling-frequency code for 48 kHz (0100, first
 *     transmitted bit at the MSB of the byte) where 0x00 is 44.1 kHz (0000).
 *     That last step keeps a hedge: the IEC 60958 channel-status layout is
 *     not an artefact in this repo, alsa_cs8427.h covers the part's registers
 *     and not the contents of the channel-status block.
 *
 * Keil encoded the constant differently in each, as it did in Rev 20: event
 * 7's zero is CLR A + MOV 0x2D,A while the 0x40 here is a direct
 * MOV 0x2D,#0x40. Both are three bytes, so this is Keil's habitual "produce
 * zero in A" rather than a size win -- the same idiom keil.peep encodes.
 *
 * ONE ENCODING DIFFERENCE FROM EVENT 7 THAT IS NOT A BEHAVIOUR DIFFERENCE.
 * Event 7 leaves its f_spdif-set arm with a 3-byte LJMP to 0x0509 and its
 * f_spdif-clear arm with a 2-byte SJMP. Event 8 is 34 bytes closer to the
 * tail, so both arms fit in short jumps: the set arm does SJMP 0x04BE and the
 * clear arm falls into that same 0x04BE, which is the single SJMP 0x0509. That
 * is why event 8 is 33 bytes to event 7's 34 despite doing identical work.
 *
 * REV 20 -> REV 22 DELTA: BEHAVIOUR IDENTICAL, HELPERS RE-CUT.
 * Same mode 3, same register 0x23 = 0x40 (0x04AF..0x04B4 here, rev20
 * 0x04A8..0x04AD -- 0x04A5 is the LJMP that ends the other arm, not part of
 * the pair), same trailing register 0x24 = 0x80. The only change is
 * where the last write happens: Rev 20 buried it in the helper at 0x0582,
 * Rev 22 stages the pair in line at 0x04B8/0x04BB and the shared tail at
 * 0x0509 writes it. Size 26 -> 33 bytes for the same reason as event 7.
 *
 * NAKED: the clock mode is Keil's R7 register argument, and both exits are the
 * dispatcher switch's `break` into a merged tail. */
void cmd8_set_clock_mode3_prog_spdif(void) __naked {
    __asm
        .globl _audio_clock_set_mode
        .globl _cs8427_write_reg04_val41
        .globl _cs8427_write_shadowed
        .globl _stage_ctrl_pair_12_00

        mov   r7,#0x03             ; clock mode 3 = 48000 Hz
        lcall _audio_clock_set_mode
        jnb   0x2c,00001$          ; BIT 0x2C = IRAM 0x25.4 = f_spdif
        lcall _cs8427_write_reg04_val41   ; reg 4 = 0x41: CLOCKSOURCE with
                                   ; RXD = AES3 input -- slave to the incoming
                                   ; S/PDIF instead of the ILRCK pin
        lcall _stage_ctrl_pair_12_00      ; stage reg 0x12 = 0x00: CSDATABUF,
                                   ; CS data, channel A, one-byte access
        sjmp  00002$
    00001$:
        mov   0x2c,#0x23           ; BYTE 0x2C = register number 0x23 =
                                   ; CORU_DATABUF + 3 = channel-status byte 3
        mov   0x2d,#0x40           ; BYTE 0x2D = value 0x40 = consumer
                                   ; sample-frequency code 0100 = 48 kHz
        lcall _cs8427_write_shadowed
        mov   0x2c,#0x24           ; BYTE 0x2C = register 0x24 = channel-status
                                   ; byte 4
        mov   0x2d,#0x80           ; BYTE 0x2D = value 0x80 = 24-bit maximum
                                   ; word length
    00002$:
        /* `sjmp _evt_tail_write_ctrl_pair` (0x0509), self-relative because
         * sdas cannot short-jump to an external symbol. `.` is area-relative,
         * so the displacement is the constant 0x49 at assembly time and
         * survives relocation: 0x04BE + 2 + 0x49 = 0x0509. */
        sjmp  . + (0x0509 - 0x04be)   ; -> 0x0509: write the pair, clear event
    __endasm;
}
