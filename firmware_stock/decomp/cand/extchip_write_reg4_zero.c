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
 * read straight from the bytes and are certain. What the registers MEAN is
 * not. IF the part is a CS8427, then register 4 is its clock-source control
 * register and writing 0 would park the clock source off while another
 * register is changed, then re-enable it -- but that is an inference stacked
 * on an unconfirmed part number. NO CS8427 DATASHEET EXISTS IN THIS REPO:
 * reference/ contains only TAS1020A/B material (reference/tas1020a/) and the
 * Digidesign firmware/updater artefacts, nothing from Cirrus Logic. And
 * firmware_stock/disasm/rev20_ANNOTATED.md:270 records the chip identity
 * itself as only "likely" (from the 0x20 chip-address byte at rev20 0x0C4B /
 * rev22 0x0C35) with the register semantics explicitly unverified. Read the
 * numbers as fact and every register name in this batch as inference.
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
