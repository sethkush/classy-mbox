// MATCH: image=rev22 addr=0x09B6 len=137 func=audio_hw_bringup cflags=--peep-file,firmware_stock/decomp/keil.peep
/* One-shot bring-up of the analogue/S-PDIF path, Rev 22 at 0x09B6.
 * Counterpart of Rev 20's audio_path_reconfig_ext_chips at 0x080B.
 *
 * It parks the 16-bit control chain, programs the TAS1020B adaptive clock
 * generators for the 48 kHz family, then pushes ten (register, value) writes
 * into the external serial audio chip over the bit-banged 3-wire port.
 *
 * ===================== REV 20 -> REV 22 DELTA ==========================
 *
 * SAME BEHAVIOUR, DIFFERENT CODE GENERATION. Every observable action is
 * identical: the same IRAM bits, the same ACG programming, the same chip
 * select pulse, and the same ten register writes in the same order with the
 * same values. Two code-generation differences account for the whole size
 * change (rev20 155 B + four helpers totalling 37 B; rev22 137 B and no
 * helpers):
 *
 * (1) THE TEN WRITES ARE INLINED. Rev 20's Keil build extracted four common
 *     block subroutines out of the ten write(reg, val) calls --
 *     extchip_write_reg4_zero (0x08A6, 13 B), extchip_write_val05 (0x08B3,
 *     10 B), extchip_write_2e_2f (0x08BD, 7 B) and its byte-identical twin
 *     extchip_write_2e_2f_dup (0x08C4, 7 B) -- and routed seven of the ten
 *     calls through them, leaving three inline. VERIFIED: none of those four
 *     helpers exists anywhere in Rev 22. Rev 22 emits all ten as a flat
 *     `MOV R7,#reg / MOV R5,#val / LCALL 0x0C31` group at 0x09F8..0x0A3D.
 *     This is the strongest available evidence that the SOURCE was always ten
 *     straight-line calls and the Rev 20 helpers were a build artefact: a
 *     hand-written helper does not vanish between revisions, and Rev 20
 *     shipped two byte-identical copies of the same three instructions.
 *
 * (2) THE ARGUMENTS NEVER TOUCH MEMORY. Rev 20 staged each pair in IRAM
 *     bytes 0x2E/0x2F and then reloaded R7/R5 from them (`MOV R5,0x2F /
 *     MOV R7,0x2E`), because the extracted helpers had to find the operands
 *     somewhere. With no helpers, Rev 22 loads R7 and R5 with the literals
 *     directly, so IRAM 0x2E and 0x2F are not used as staging here at all.
 *
 * (3) THE SETTLE DELAY MOVED FROM MEMORY TO A REGISTER. Rev 20 spends IRAM
 *     BYTE 0x2E on it (`MOV 0x2E,#0xFF / DJNZ 0x2E,$`, 3+3 = 6 bytes each,
 *     four times). Rev 22 uses R7 (`MOV R7,#0xFF / DJNZ R7,$`, 2+2 = 4 bytes
 *     each, at 0x09BD, 0x09D4, 0x09DF, 0x09EA). VERIFIED against the bytes:
 *     rev22 has 7F FF / DF FE at all four sites. R7 is free in Rev 22 exactly
 *     because (2) freed it -- there are no live staged arguments to protect.
 *     Same iteration count (255), so the delay is unchanged.
 *
 * NOTE THE BIT/BYTE COLLISION THIS RESOLVES. In Rev 20, `SETB 0x2E` (bit
 * 0x2E = IRAM 0x25.6, the init-done flag) and `MOV 0x2E,#0xFF` (IRAM BYTE
 * 0x2E, the delay counter) sit two instructions apart and print identically.
 * Rev 22 keeps the SETB (0x09BB) but the byte 0x2E use is gone, so the
 * collision only exists in Rev 20.
 *
 * Callee addresses are the only other change, and they are pure relocation:
 *
 *     what                        rev20      rev22
 *     16-bit shift-register out   0x0E62     0x0E56   (shiftreg_out16_p1)
 *     ACG synths -> 24.576 MHz    0x0DEC     0x0EC8   (acg_both_synths_...)
 *     ACG dividers /2 entry       0x0E17     0x0EF3   (acg_dividers_div2)
 *     3-wire chip write           0x0C45     0x0C31   (spi3wire_write_3bytes)
 *
 * ======================== WHAT IT DOES =================================
 *
 * RUN-ONCE, ENFORCED BY THE CALLERS. `SETB 0x2E` at 0x09BB sets bit 0x2E =
 * IRAM 0x25 bit 6, and nothing ever clears it. All four call sites test it
 * first and skip: rev22 0x0366, 0x0396, 0x0419, 0x04CB (rev20 0x035D, 0x038F,
 * 0x0416, 0x04C4). That bit is the "external chips already brought up" flag.
 *
 * THE 16-BIT CONTROL CHAIN. IRAM 0x23 (low) and 0x25 (high) are the two
 * payload bytes shifted out on P1 by shiftreg_out16_p1 (0x0E56). Bit B lives
 * in IRAM 0x20+(B>>3) at bit B&7, so bits 0x18..0x1F are IRAM 0x23.0..7 and
 * 0x28..0x2F are IRAM 0x25.0..7. Of the bits touched here, 0x25.6 (init-done)
 * and 0x25.7 (serial chip select) are pinned; 0x23.2, 0x23.3 and 0x23.4 are
 * NOT identified -- the same three Rev 20 leaves unresolved. All that is known
 * is their timing: 0x23.2 and 0x23.3 are held low across clock reprogramming
 * and released once the clock is stable (rev22 audio_clock_set_mode clears
 * them at 0x0716/0x0718).
 *
 * THE BARE CHIP-SELECT PULSE at 0x09EE..0x09F7 -- CS low, latch, CS high,
 * latch -- carries no data and precedes all ten writes. Every register write
 * already brackets itself with CS inside spi3wire_write_3bytes (CLR 0x2F at
 * 0x0C39, SETB 0x2F at 0x0C77), so this one is extra. A control port being
 * told which of two protocol modes to use would explain it; that is an
 * INFERENCE and it is STILL not verified. Identifying the part (see
 * FINDING_cs8427_confirmed.md) does not settle it: the artefact this repo has,
 * reference/cs8427/alsa_cs8427.h, is a REGISTER map and says nothing about the
 * part's mode-selection pin behaviour. Rev 20 emits the identical pulse at
 * 0x084B..0x0854.
 *
 * THE TEN REGISTER WRITES, read straight from the bytes:
 *
 *     reg 0x04 = 0x00     rev22 0x09F8     rev20 0x0855 (via helper 0x08A6)
 *     reg 0x13 = 0x10     rev22 0x09FF     rev20 0x0858 (via helper 0x08BD)
 *     reg 0x04 = 0x00     rev22 0x0A06     rev20 0x0861 (via helper 0x08A6)
 *     reg 0x04 = 0x40     rev22 0x0A0D     rev20 0x0864 (via helper 0x08BD)
 *     reg 0x01 = 0x01     rev22 0x0A14     rev20 0x086D (via helper 0x08C4)
 *     reg 0x02 = 0x20     rev22 0x0A1B     rev20 0x0876 (via helper 0x08C4)
 *     reg 0x03 = 0x0C     rev22 0x0A22     rev20 0x087F (inline)
 *     reg 0x05 = 0x05     rev22 0x0A29     rev20 0x088C (via helper 0x08B3)
 *     reg 0x06 = 0x05     rev22 0x0A30     rev20 0x0892 (via helper 0x08B3)
 *     reg 0x11 = 0xFF     rev22 0x0A37     rev20 0x0898 (inline)
 *
 * Those pairs were re-read off the bytes for this annotation, not copied
 * forward: rev22 0x09F8 is 7f 04 e4 fd 12 0c 31 7f 13 7d 10 12 0c 31 (R7 =
 * register, R5 = value; reg 0x04 = 0x00 then reg 0x13 = 0x10) and rev22 0x0A22
 * is 7f 03 7d 0c 12 0c 31 (reg 0x03 = 0x0C). The rev20 column was checked the
 * same way: rev20 0x0855 is 12 08 a6 75 2e 13 75 2f 10 12 08 bd, and the
 * helper it calls first, extchip_write_reg4_zero, is 75 2e 04 e4 f5 2f at
 * rev20 0x08A6 -- register 4, value zero.
 *
 * ================= WHAT THE TEN WRITES MEAN ==========================
 *
 * Register NUMBERS and VALUES were always certain; the NAMES used to be
 * flagged here as unclaimed, because no CS8427 datasheet exists anywhere under
 * reference/ (TAS1020A/B material and Digidesign updater artefacts only) and
 * the chip identification was itself rated only "likely" from the constant
 * 0x20 lead byte at rev22 0x0C35 / rev20 0x0C4B.
 *
 * BOTH OF THOSE HAVE MOVED. The part is identified -- 0x20 is an I2C write to
 * slave address 0x10, which ALSA's header gives as CS8427_BASE_ADDR, and every
 * register the firmware touches decodes coherently under that map; see
 * FINDING_cs8427_confirmed.md. And the register map is now in the repo as
 * reference/cs8427/alsa_cs8427.h. That header is a SECONDARY source (ALSA's
 * named constants, not Cirrus's datasheet), so names below are given as what
 * ALSA's CS8427 header calls the field.
 *
 * The full field-by-field decode of all ten values is written up once, in the
 * Rev 20 counterpart audio_path_reconfig_ext_chips.c; the values are identical
 * so it is not repeated here. In brief:
 *
 *   0x04 = 0x00   CLOCKSOURCE, RUN = 0 -- stop the clock before reconfiguring
 *   0x13 = 0x10   UDATABUF, UD = 1 (U pin an output), UBM = 00 (send zeros)
 *   0x04 = 0x00   still stopped
 *   0x04 = 0x40   CLOCKSOURCE, RUN = 1, 256*Fso, RXD = ILRCK pin -- clocked
 *                 from the TAS1020B, NOT from the incoming S/PDIF. cmd7/cmd8
 *                 write 0x41 here instead when S/PDIF is the source, which is
 *                 the same register with RXD = AES3 input.
 *   0x01 = 0x01   CONTROL1, TCBLDIR = 1 (TCBL is an output), no mutes
 *   0x02 = 0x20   CONTROL2, HOLD = 01 = "replace the current audio sample with
 *                 zero (mute)" on a receiver error; stereo RX and stereo TX
 *   0x03 = 0x0C   DATAFLOW -- the whole S/PDIF routing in one byte. TXD field
 *                 = TXDSERIAL: the AES3 transmitter is fed from the serial
 *                 audio input port, so host playback goes out S/PDIF. SPD
 *                 field = SPDAES3RECEIVER: the serial audio output port is fed
 *                 from the AES3 receiver, so S/PDIF in reaches capture.
 *                 TXOFF = 0 and AESBP = 0: transmitter on, no RX->TX bypass.
 *   0x05 = 0x05   SERIALINPUT, slave, 24-bit, left-justified + one-clock delay
 *                 + inverted LRCK polarity = I2S
 *   0x06 = 0x05   SERIALOUTPUT, the same format in the mirror register
 *   0x11 = 0xFF   RECVERRMASK, every defined receiver-error bit set. The ALSA
 *                 header gives this register the bit layout of RECVERRORS but
 *                 NOT the polarity of the mask, so "all receiver errors are
 *                 silent" is the reading, not a decode -- keep that hedge.
 *                 What is certain from the bytes is that no receiver-error
 *                 interrupt is enabled either way: INT1MASK (0x09) and
 *                 INT2MASK (0x0C) are never written by either image.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY, for one reason that survives the loss of
 * the helpers: spi3wire_write_3bytes takes its arguments in R7 (register) and
 * R5 (value), Keil's register-parameter convention, which SDCC does not use
 * -- it would pass the first char in DPL. The Rev 20 candidate needed asm for
 * the common-block extraction as well; Rev 22 needs it only for this.
 */
void audio_hw_bringup(void) __naked {
    __asm
        .globl _shiftreg_out16_p1
        .globl _acg_both_synths_24576khz
        .globl _acg_dividers_div2
        .globl _spi3wire_write_3bytes

        ; ---- 0x09B6: park the control chain, claim the init-done bit -------
        clr   a
        mov   0x25,a               ; chain high byte = 0
        mov   0x23,a               ; chain low  byte = 0
        setb  0x2e                 ; BIT 0x2E = IRAM 0x25.6: bring-up has run.
                                   ; The four callers test this and skip.
        mov   r7,#0xff             ; settle delay -- R7, not IRAM byte 0x2E as
    0001$:                         ; in rev20. See delta note (3).
        djnz  r7,0001$
        lcall _shiftreg_out16_p1   ; 0x0E56: shifts IRAM 0x23 then 0x25 out on P1

        ; ---- 0x09C4: clock generators to the 48 kHz family -----------------
        lcall _acg_both_synths_24576khz ; 0x0EC8: ACG1/ACG2 FRQ = 0x61A80F,
                                   ; falling through 0x0EE8 which stores the
                                   ; last FRQ byte and sets ACGCTL (0xFFE1) = 6.
                                   ; Returns with DPTR still on 0xFFE1.
        lcall _acg_dividers_div2   ; 0x0EF3: INC DPTR (0xFFE1 -> 0xFFE2), then
                                   ; falls into 0x0EF4: ACG1DCTL = ACG2DCTL =
                                   ; 0x10. Entering one byte early is how the
                                   ; caller reuses the DPTR left by the call
                                   ; above -- the same merged-tail trick rev20
                                   ; uses at its 0x0E17.
        mov   0x08,#0x03           ; g_clock_mode = 3 = 48 kHz. Same IRAM byte
                                   ; audio_clock_set_mode writes and
                                   ; setup_get_sample_freq reads back.
        mov   dptr,#0xffe1         ; ACGCTL
        movx  a,@dptr
        orl   a,#0xc0              ; MCLKO1EN | MCLKO2EN: master clock outputs on
        movx  @dptr,a

        ; ---- 0x09D4: release two control lines, one latch at a time --------
        mov   r7,#0xff
    0002$:
        djnz  r7,0002$
        setb  0x1a                 ; IRAM 0x23.2 -- function not identified
        setb  0x1b                 ; IRAM 0x23.3 -- function not identified
        lcall _shiftreg_out16_p1

        mov   r7,#0xff
    0003$:
        djnz  r7,0003$
        setb  0x2f                 ; IRAM 0x25.7 = serial chip select, idle high
        setb  0x1c                 ; IRAM 0x23.4 -- function not identified
        lcall _shiftreg_out16_p1

        ; ---- 0x09EA: a bare low pulse on chip select, no data --------------
        mov   r7,#0xff
    0004$:
        djnz  r7,0004$
        clr   0x2f                 ; CS low
        lcall _shiftreg_out16_p1
        setb  0x2f                 ; CS high again
        lcall _shiftreg_out16_p1

        ; ---- 0x09F8: ten control-register writes, all inline ---------------
        mov   r7,#0x04
        clr   a                    ; Keil's zero encoding: CLR A / MOV R5,A
        mov   r5,a
        lcall _spi3wire_write_3bytes    ; reg 0x04 = 0x00: CLOCKSOURCE, RUN = 0
        mov   r7,#0x13
        mov   r5,#0x10
        lcall _spi3wire_write_3bytes    ; reg 0x13 = 0x10: UDATABUF, UD = out,
                                        ; U-bit manager = transmit zeros
        mov   r7,#0x04
        clr   a
        mov   r5,a
        lcall _spi3wire_write_3bytes    ; reg 0x04 = 0x00 again, still stopped
        mov   r7,#0x04
        mov   r5,#0x40
        lcall _spi3wire_write_3bytes    ; reg 0x04 = 0x40: RUN = 1, 256*Fso,
                                        ; RXD = ILRCK pin (not S/PDIF)
        mov   r7,#0x01
        mov   r5,#0x01
        lcall _spi3wire_write_3bytes    ; reg 0x01 = 0x01: CONTROL1, TCBL out
        mov   r7,#0x02
        mov   r5,#0x20
        lcall _spi3wire_write_3bytes    ; reg 0x02 = 0x20: CONTROL2, HOLD = 01
                                        ; = zero the sample on receiver error
        mov   r7,#0x03
        mov   r5,#0x0c
        lcall _spi3wire_write_3bytes    ; reg 0x03 = 0x0C: DATAFLOW. Playback
                                        ; -> AES3 TX, AES3 RX -> capture.
        mov   r7,#0x05
        mov   r5,#0x05
        lcall _spi3wire_write_3bytes    ; reg 0x05 = 0x05: SERIALINPUT, slave
                                        ; 24-bit I2S
        mov   r7,#0x06
        mov   r5,#0x05
        lcall _spi3wire_write_3bytes    ; reg 0x06 = 0x05: SERIALOUTPUT, same
        mov   r7,#0x11
        mov   r5,#0xff
        lcall _spi3wire_write_3bytes    ; reg 0x11 = 0xFF: RECVERRMASK, all
                                        ; defined error bits set
        ret
    __endasm;
}
