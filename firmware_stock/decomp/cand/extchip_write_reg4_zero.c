// MATCH: image=rev20 addr=0x08A6 len=13 func=extchip_write_reg4_zero cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Common-block subroutine: write external-chip register 4 = 0x00.
 *
 * Same class as extchip_write_2e_2f (0x08BD) -- a block Keil's optimiser
 * pulled out of audio_path_reconfig_ext_chips (0x080B) because it occurs
 * twice verbatim, at 0x0855 and 0x0861. Here the extracted block includes the
 * two argument stores as well, because both call sites use the same literal
 * arguments; the source is just `write(4, 0)` written twice.
 *
 * Note the shape of the constant-zero store: `CLR A / MOV 0x2F,A` rather than
 * `MOV 0x2F,#0`. That is Keil's standard encoding for a zero store (2+2 bytes
 * against 3) and is exactly what keil.peep's `mov dir,#0 -> clr a; mov dir,a`
 * rule exists to reproduce; here it is written out literally because the whole
 * function is __naked.
 *
 * Why register 4 is written twice, before and after register 0x13: the
 * sequence in the caller is reg 4 = 0x00, reg 0x13 = 0x10, reg 4 = 0x00,
 * reg 4 = 0x40 (0x0855..0x086C). Those register numbers and those values are
 * read straight from the bytes and are certain. WHAT THEY MEAN IS NOW ALSO
 * ESTABLISHED, which it was not when this comment was first written.
 *
 * The part is a Cirrus Logic CS8427 (firmware_stock/decomp/FINDING_cs8427_
 * confirmed.md). ALSA's CS8427 header names register 0x04
 * CS8427_REG_CLOCKSOURCE and gives CS8427_RUN = (1<<6) with "0 = clock off,
 * 1 = clock on". So:
 *
 *     reg 4 = 0x00   RUN = 0  -- clock STOPPED
 *     reg 4 = 0x40   RUN = 1  -- clock STARTED; the rest of the byte is
 *                               CLK256 (bits 5:4 = 00, 256*Fso), OUTC = 0
 *                               (output time base = OMCK), INC = 0 (input
 *                               time base = recovered input clock), and
 *                               RXD = 00 = CS8427_RXDILRCK (256*Fsi from the
 *                               ILRCK pin)
 *
 * The stop-clock / reconfigure / start-clock reading this comment previously
 * called "an inference stacked on an inference" is therefore CORRECT and no
 * longer an inference: the bracket parks the clock off, writes register 0x13,
 * and turns it back on. Register 0x04 really is the clock-source register.
 * (ALSA's header is a secondary source -- cite it as "ALSA's CS8427 header
 * names this ...", not as datasheet text. No Cirrus datasheet is in this repo;
 * reference/ has TAS1020A/B material, the Digidesign artefacts and that
 * header.)
 *
 * THE REGISTER THE BRACKET PROTECTS IS ONLY HALF DECODED. ALSA names 0x13
 * CS8427_REG_UDATABUF, so it is the AES3 U-bit (user-data) buffer control
 * register, and that much is settled. Why 0x10 specifically is NOT: the header
 * gives CS8427_UD = (1<<4) as the U-pin direction bit, which 0x10 sets, and
 * CS8427_UBMMASK = (3<<2) as the U-bit manager mode with bits 3:2 = 00 here,
 * for which it names only two of the four codes. Whether "U pin as output,
 * U-bit manager in its 00 mode" is what the firmware author was after, or a
 * side effect of wanting one of the other bits clear, is not decidable from
 * the register map alone. Read 0x13 = 0x10 as a named register with an
 * undecoded value.
 *
 * REV 22 CROSS-CHECK: rev22 has no helper here at all -- neither this one nor
 * extchip_write_val05 nor extchip_write_2e_2f exists in that image. Rev 22's
 * audio_hw_bringup (0x09B6, the counterpart of rev20's
 * audio_path_reconfig_ext_chips at 0x080B) inlines all ten (register, value)
 * pairs as MOV R7 / MOV R5 / LCALL 0x0C31 at 0x09F8-0x0A3D, in the same order
 * and with the same values: 4=0x00, 0x13=0x10, 4=0x00, 4=0x40, 1=0x01,
 * 2=0x20, 3=0x0C, 5=0x05, 6=0x05, 0x11=0xFF. Both reg 4 = 0x00 writes are
 * there (rev22 0x09F8 and 0x0A06) and both use the same `MOV R7,#0x4 / CLR A
 * / MOV R5,A` zero encoding, so the constant-zero store survives even without
 * the extraction. The wire sequence is identical between the images; only the
 * common-block factoring differs.
 *
 * __naked: SDCC has no common-block extraction, and cs8427_ctl_write takes
 * its arguments in R7 (register) and R5 (value), Keil's register-parameter
 * convention.
 */
void extchip_write_reg4_zero(void) __naked {
    __asm
        .globl _cs8427_ctl_write
        mov   0x2e,#0x04       ; staged register number  (IRAM byte 0x2E)
        clr   a
        mov   0x2f,a           ; staged value = 0        (IRAM byte 0x2F)
        mov   r5,0x2f
        mov   r7,0x2e
        ljmp  _cs8427_ctl_write ; 0x0C45
    __endasm;
}
