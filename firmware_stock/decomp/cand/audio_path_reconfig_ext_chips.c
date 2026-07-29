// MATCH: image=rev20 addr=0x080B len=155 func=audio_path_reconfig_ext_chips cflags=--peep-file,firmware_stock/decomp/keil.peep
/* One-shot bring-up of the analogue/S-PDIF path: latch the 16-bit control
 * chain into a known state, program the TAS1020B clock generators for the
 * 48 kHz family, then push ten control-register writes into the external
 * serial audio chip.
 *
 * RUN-ONCE, AND IT IS THE CALLERS THAT SAY SO. All four call sites are
 * guarded by the same two instructions:
 *
 *     JB   0x2E, skip        ; bit 0x2E = IRAM 0x25.6
 *     LCALL 0x080B
 *
 * at 0x035D/0x0360 (cmd1_apply_clock_mode), 0x038F/0x0392
 * (cmd2_apply_iface1_alt), 0x0416/0x0419 (cmd3_apply_iface2_alt) and
 * 0x04C4/0x04C7 (cmd11_eeprom_selftest). This function sets that same bit at
 * 0x0810 and never clears it, so the second and later calls are skipped by the
 * caller. Rev 22 is identical in shape: audio_hw_bringup at 0x09B6 sets bit
 * 0x2E at 0x09BB and its callers guard at 0x0366, 0x0396, 0x0419 and 0x04CB.
 * That is what pins IRAM 0x25.6 as "external chips already brought up", the
 * reading firmware_stock/disasm/PANEL_LEDS.md records as "hw-init-done".
 *
 * THE BIT/BYTE TRAP, IN ONE FUNCTION. `SETB 0x2E` at 0x0810 sets *bit* 0x2E,
 * which is IRAM 0x25 bit 6. `MOV 0x2E,#0xFF` at 0x0812 writes IRAM *byte*
 * 0x2E, an ordinary scratch location. They are unrelated storage and the two
 * print identically in a listing. The same pair occurs for 0x2F: `SETB 0x2F`
 * / `CLR 0x2F` (0x083E, 0x084B, 0x0850) drive IRAM 0x25 bit 7, the serial
 * chip-select line, while `MOV 0x2F,#0x10` (0x085B) stages a register value in
 * IRAM byte 0x2F.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY, and for a reason distinct from
 * hw_master_init's. The blocker here is not register allocation, it is that
 * Keil extracted four *common block subroutines* out of the ten
 * write(reg, val) calls at the bottom: 0x08A6, 0x08B3, 0x08BD and 0x08C4. Some
 * of the ten calls go through a helper and some are emitted inline, purely
 * according to which blocks the optimiser happened to group. SDCC has no such
 * transform, so no C source reproduces the call pattern, and the callee's
 * arguments are passed in R7/R5 (Keil's register-parameter convention) which
 * SDCC does not use either.
 *
 * The evidence that the source really was ten flat calls is Rev 22, which
 * compiles the same sequence with no helpers at all: audio_hw_bringup 0x09F8
 * onwards is ten literal `MOV R7,#reg / MOV R5,#val / LCALL 0x0C31` groups,
 * same registers, same values, same order. Rev 22 also spends R7 on the
 * settle delay (`MOV R7,#0xFF / DJNZ R7,$` at 0x09BD) where Rev 20 uses IRAM
 * byte 0x2E -- a register-allocation difference, not a source difference.
 *
 * THE 16-BIT CONTROL CHAIN. IRAM 0x23 and 0x25 are the two payload bytes
 * shifted out by shiftreg16_commit (0x0E62; rev22 shiftreg_out16_p1 at
 * 0x0E56). Bit numbering: bit B lives in IRAM 0x20+(B>>3) at bit B&7, so
 * 0x18..0x1F are IRAM 0x23.0..7 and 0x28..0x2F are IRAM 0x25.0..7. Of the bits
 * touched here, 0x25.6 (init-done) and 0x25.7 (serial chip select) are pinned;
 * 0x23.2, 0x23.3 and 0x23.4 are NOT identified. What is known about them is
 * only the timing: 0x23.2 and 0x23.3 are also cleared at the top of
 * audio_clock_mode_apply (0x072F/0x0731) and set again in its tail
 * (0x07EE/0x07F0), i.e. they are held low across any clock reprogramming and
 * released when the clock is stable. PANEL_LEDS.md notes this chain is a mixed
 * LED/control word and that the individual assignments are unresolved; nothing
 * here resolves them, so no function is claimed for those three bits.
 *
 * THE CHIP-SELECT PULSE at 0x084B..0x0854 -- CS driven low, latched, driven
 * high, latched -- is a bare pulse with no data. Every register write already
 * brackets itself with CS (cs8427_ctl_write clears bit 0x2F at 0x0C4F and sets
 * it at 0x0C8D), so this one is extra and precedes all ten writes. A control
 * port that has to be told which of its two protocol modes to use would be the
 * obvious explanation; that is an INFERENCE and is STILL not verified. The
 * part itself is no longer in doubt -- see FINDING_cs8427_confirmed.md -- but
 * identifying the part does not explain this pulse: the artefact this repo has
 * for it, reference/cs8427/alsa_cs8427.h, is a REGISTER map, and nothing in it
 * describes the part's mode-selection pin behaviour. No CS8427 datasheet
 * exists in this repo. Rev 22 emits the identical pulse at 0x09EE..0x09F7.
 *
 * THE TEN REGISTER WRITES, in order, exactly as the bytes give them:
 *
 *     reg 0x04 = 0x00      0x0855 -> helper 0x08A6
 *     reg 0x13 = 0x10      0x0858..0x0860
 *     reg 0x04 = 0x00      0x0861 -> helper 0x08A6
 *     reg 0x04 = 0x40      0x0864..0x086C
 *     reg 0x01 = 0x01      0x086D..0x0875
 *     reg 0x02 = 0x20      0x0876..0x087E
 *     reg 0x03 = 0x0C      0x087F..0x088B (inlined, not via a helper)
 *     reg 0x05 = 0x05      0x088C..0x0891 -> helper 0x08B3
 *     reg 0x06 = 0x05      0x0892..0x0897 -> helper 0x08B3
 *     reg 0x11 = 0xFF      0x0898..0x08A4 (inlined)
 *
 * Rev 22 writes the same ten pairs in the same order at 0x09F8..0x0A3D, and
 * the pairs were re-read off both images for this annotation rather than
 * copied forward: rev20 0x0855 is 12 08 a6 75 2e 13 75 2f 10 12 08 bd (call
 * the reg-4-zero helper, then stage 0x13/0x10 and call the pair helper), and
 * rev22 0x09F8 is 7f 04 e4 fd 12 0c 31 7f 13 7d 10 12 0c 31 (R7=reg, R5=val,
 * the same two writes inline). The two reg 0x04 = 0x00 writes are inside
 * extchip_write_reg4_zero, whose body at rev20 0x08A6 is 75 2e 04 e4 f5 2f --
 * register 4, value zero.
 *
 * ==================== WHAT THE TEN WRITES MEAN ========================
 *
 * The register NUMBERS and VALUES were always certain; the NAMES used to be
 * flagged as unverified inference. They are now decoded against
 * reference/cs8427/alsa_cs8427.h, which is the CS8427 register map expressed
 * as named constants with bit meanings. It is a SECONDARY source -- ALSA's
 * header, not Cirrus's datasheet -- so every claim below is phrased as what
 * ALSA's header names the field, and the bit arithmetic is shown so it can be
 * rechecked.
 *
 *  1. reg 0x04 = 0x00  CLOCKSOURCE. ALSA names bit 6 RUN, "0 = clock off".
 *     The clock is stopped before anything else is reconfigured.
 *
 *  2. reg 0x13 = 0x10  UDATABUF, the AES3 user-data buffer control. ALSA
 *     names bit 4 UD, "User data pin (U) direction, 0 = input, 1 = output",
 *     and bits 3:2 UBM, the U-bit manager mode, with 00 = UBMZEROS,
 *     "transmit all zeros mode". 0x10 is therefore UD = 1 with UBM = 00: the
 *     U pin is an output and the transmitter sends all-zero user data. Note
 *     that this header defines DETUI and EFTUI as the SAME bit, (1<<1), which
 *     is plainly a typo in the secondary source; readings of the low bits of
 *     this register should not be trusted from it, and none are made here.
 *
 *  3. reg 0x04 = 0x00  CLOCKSOURCE again -- clock still stopped while the
 *     write above lands.
 *
 *  4. reg 0x04 = 0x40  CLOCKSOURCE, RUN = 1: clock on. The other fields are
 *     all zero, which ALSA reads as CLK256 (bits 5:4 = 00, OMCK = 256*Fso),
 *     OUTC = 0 (output time base = OMCK, not the recovered input clock),
 *     INC = 0 (input time base = recovered input clock) and RXD = 00
 *     (RXDILRCK, "256*Fsi from ILRCK pin"). So in the default bring-up state
 *     the part is clocked from the TAS1020B's own audio clock, NOT from the
 *     incoming S/PDIF. The 0x00 / other / 0x00 / 0x40 bracket that used to be
 *     described here as "the only structure visible without a datasheet" is
 *     exactly stop-clock ... start-clock, and that earlier guess was right.
 *
 *  5. reg 0x01 = 0x01  CONTROL1. Bit 0 is TCBLDIR, "1 = TCBL is an output".
 *     Everything else clear: SWCLK = 0 (RMCK default), VSET = 0 (valid PCM
 *     data), MUTESAO = 0 and MUTEAES = 0 (neither the serial audio output nor
 *     the AES3 transmitter is muted), INT pin active high.
 *
 *  6. reg 0x02 = 0x20  CONTROL2. Bits 6:5 are HOLD, the action taken when a
 *     receiver error occurs; 0x20 is (1<<5) = HOLDZERO, which ALSA glosses as
 *     "replace the current audio sample with zero (mute)". On a bad or absent
 *     S/PDIF input the captured samples go to silence rather than repeating
 *     the last good sample. The rest is zero: RMCKF = 0 (256*Fsi), MMR = 0
 *     and MMT = 0, i.e. the AES3 receiver and transmitter both run stereo,
 *     not mono.
 *
 *  7. reg 0x03 = 0x0C  DATAFLOW -- THE WHOLE S/PDIF ROUTING IN ONE BYTE, and
 *     the one line nobody had claimed before. Two fields:
 *       - TXD, bits 4:3, the AES3 transmitter's data source. 0x0C & (3<<3)
 *         = 0x08 = (1<<3) = TXDSERIAL, "TXD - serial audio input port". The
 *         AES3 (S/PDIF) transmitter is fed from the serial audio input port,
 *         i.e. from the TAS1020B -- so whatever the host plays out is what
 *         goes down the S/PDIF output. The alternative encoding, (2<<3) =
 *         TXAES3DRECEIVER, would have made the output a copy of the input.
 *       - SPD, bits 2:1, the serial audio OUTPUT port's data source. 0x0C &
 *         (3<<1) = 0x04 = (2<<1) = SPDAES3RECEIVER, "SPD - AES3 receiver".
 *         The serial audio output port -- the side the TAS1020B captures
 *         from -- is fed by the AES3 receiver, i.e. by the incoming S/PDIF.
 *     The two remaining bits are clear: TXOFF = 0 (transmitter output normal,
 *     not forced to 0 V) and AESBP = 0 (no hardware RX->TX bypass).
 *     So: playback -> S/PDIF out, S/PDIF in -> capture, full duplex, with the
 *     part in the middle rather than bypassed. This is the routing half of
 *     what task #145 (S/PDIF clock slaving) needs; write 4 above is the clock
 *     half, and cmd7/cmd8 are where the clock half gets switched.
 *
 *  8. reg 0x05 = 0x05  SERIALINPUT, the format of the port the TAS1020B
 *     drives into the CS8427. 0x05 = SIDEL (1<<2) | SILRPOL (1<<0), with
 *     SIMS = 0 (slave -- the TAS1020B provides ISCLK/ILRCK), SISF = 0
 *     (64*Fsi), SIRES = 00 (SIRES24, 24-bit) and SIJUST = 0 (left-justified).
 *     Left-justified, data delayed one clock (SIDEL = "second ISCLK period"),
 *     LRCK polarity inverted (SILRPOL = "SDIN right channel when ILRCK is
 *     high") is the definition of I2S.
 *
 *  9. reg 0x06 = 0x05  SERIALOUTPUT, the same value in the mirror-image
 *     register: SOMS = 0 slave, 64*Fso, SORES = 00 (24-bit), SODEL and
 *     SOLRPOL set. Both directions are 24-bit I2S in slave mode, which is
 *     consistent with the TAS1020B owning the clocks.
 *
 * 10. reg 0x11 = 0xFF  RECVERRMASK, the receiver-error mask. ALSA gives this
 *     register the same bit layout as RECVERRORS -- QCRC, CCRC, UNLOCK, V,
 *     CONF, BIP, PAR -- so 0xFF covers every defined error condition plus the
 *     unused top bit. The intent reads as "the firmware does not want to hear
 *     about receiver errors": no interrupt is ever wired up for them
 *     (INT1MASK, register 0x09, and INT2MASK, register 0x0C, are never
 *     written by either image -- the only registers either image ever writes
 *     are 0x01..0x06, 0x11, 0x12, 0x13, 0x23 and 0x24), and CONTROL2's HOLD
 *     field above already handles a receiver error in hardware by muting.
 *     THE HEDGE THAT REMAINS: the ALSA header states the bit MEANINGS for
 *     this register but not the polarity of the mask -- whether a 1 suppresses
 *     the condition or enables it to assert RERR. Either way no interrupt is
 *     enabled, so the audible behaviour is the same; but do not quote "0xFF
 *     masks every error" as a decoded fact, it is the reading, not the map.
 */
void audio_path_reconfig_ext_chips(void) __naked {
    __asm
        .globl _shiftreg16_commit
        .globl _acg_48k_commit
        .globl _acg_incdptr_dctl_div2
        .globl _extchip_write_reg4_zero
        .globl _extchip_write_2e_2f
        .globl _extchip_write_2e_2f_dup
        .globl _extchip_write_val05
        .globl _cs8427_ctl_write

        ; ---- 0x080B: park the 16-bit control chain, claim the init-done bit --
        clr   a
        mov   0x25,a               ; chain high byte = 0
        mov   0x23,a               ; chain low  byte = 0
        setb  0x2e                 ; BIT 0x2E = IRAM 0x25.6: bring-up has run.
                                   ; The callers test this to skip the call.
        mov   0x2e,#0xff           ; BYTE 0x2E: settle counter. Unrelated storage.
    0001$:
        djnz  0x2e,0001$           ; ~255 iterations before the first latch
        lcall _shiftreg16_commit   ; 0x0E62: shifts IRAM 0x23 first (loaded at
                                   ; 0x0E64), then 0x25, out on P1

        ; ---- 0x081B: clock generators to the 48 kHz family -------------------
        lcall _acg_48k_commit      ; 0x0DEC: ACG1/ACG2 FRQ = 0x61A80F, ACGCTL = 0x06.
                                   ; Returns with DPTR left on ACGCTL (0xFFE1,
                                   ; loaded 0x0E10, stored 0x0E15).
        lcall _acg_incdptr_dctl_div2 ; 0x0E17: INC DPTR (0xFFE1 -> 0xFFE2), then
                                   ; ACG1DCTL = ACG2DCTL = 0x10. Entering one
                                   ; byte before 0x0E18 is how the caller reuses
                                   ; the DPTR the previous call left behind.
        mov   0x08,#0x03           ; g_clock_mode = 3 = 48 kHz. Same IRAM byte
                                   ; audio_clock_mode_apply writes (0x0791) and
                                   ; setup_get_sample_freq reads back.
        mov   dptr,#0xffe1         ; ACGCTL
        movx  a,@dptr
        orl   a,#0xc0              ; MCLKO1EN | MCLKO2EN: master clock outputs on
        movx  @dptr,a

        ; ---- 0x082B: release two control lines, one latch at a time ----------
        mov   0x2e,#0xff
    0002$:
        djnz  0x2e,0002$
        setb  0x1a                 ; IRAM 0x23.2 -- function not identified
        setb  0x1b                 ; IRAM 0x23.3 -- function not identified
        lcall _shiftreg16_commit

        mov   0x2e,#0xff
    0003$:
        djnz  0x2e,0003$
        setb  0x2f                 ; IRAM 0x25.7 = serial chip select, idle high
        setb  0x1c                 ; IRAM 0x23.4 -- function not identified
        lcall _shiftreg16_commit

        ; ---- 0x0845: a bare low pulse on chip select, no data ---------------
        mov   0x2e,#0xff
    0004$:
        djnz  0x2e,0004$
        clr   0x2f                 ; CS low
        lcall _shiftreg16_commit
        setb  0x2f                 ; CS high again
        lcall _shiftreg16_commit

        ; ---- 0x0855: ten control-register writes ----------------------------
        lcall _extchip_write_reg4_zero   ; reg 4 = 0x00: CLOCKSOURCE, RUN = 0.
                                         ; Stop the clock before reconfiguring.
        mov   0x2e,#0x13
        mov   0x2f,#0x10
        lcall _extchip_write_2e_2f       ; reg 0x13 = 0x10: UDATABUF, UD = 1
                                         ; (U pin is an output), UBM = 00
                                         ; (transmit all zeros).
        lcall _extchip_write_reg4_zero   ; reg 4 = 0x00 again -- still stopped
        mov   0x2e,#0x04
        mov   0x2f,#0x40
        lcall _extchip_write_2e_2f       ; reg 4 = 0x40: CLOCKSOURCE, RUN = 1,
                                         ; 256*Fso, RXD = ILRCK pin. Clocked by
                                         ; the TAS1020B, not by S/PDIF.
        mov   0x2e,#0x01
        mov   0x2f,#0x01
        lcall _extchip_write_2e_2f_dup   ; reg 1 = 0x01: CONTROL1, TCBLDIR = 1
                                         ; (TCBL is an output), no mutes.
                                         ; (the other identical helper copy)
        mov   0x2e,#0x02
        mov   0x2f,#0x20
        lcall _extchip_write_2e_2f_dup   ; reg 2 = 0x20: CONTROL2, HOLD = 01 =
                                         ; replace sample with zero on a
                                         ; receiver error. Stereo RX and TX.
        mov   0x2e,#0x03
        mov   0x2f,#0x0c
        mov   r5,0x2f                    ; this one the optimiser left inline
        mov   r7,0x2e
        lcall _cs8427_ctl_write           ; reg 3 = 0x0C: DATAFLOW. TXD = serial
                                         ; input port -> AES3 transmitter;
                                         ; SPD = AES3 receiver -> serial output
                                         ; port. The whole S/PDIF path.
        mov   0x2e,#0x05
        lcall _extchip_write_val05       ; reg 5 = 0x05: SERIALINPUT, slave,
                                         ; 24-bit, I2S framing
        mov   0x2e,#0x06
        lcall _extchip_write_val05       ; reg 6 = 0x05: SERIALOUTPUT, same
        mov   0x2e,#0x11
        mov   0x2f,#0xff
        mov   r5,0x2f                    ; inline again -- last block, no pair
        mov   r7,0x2e
        lcall _cs8427_ctl_write           ; reg 0x11 = 0xFF: RECVERRMASK, every
                                         ; defined receiver-error bit set
        ret
    __endasm;
}
