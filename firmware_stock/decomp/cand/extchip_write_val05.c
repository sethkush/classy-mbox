// MATCH: image=rev20 addr=0x08B3 len=10 func=extchip_write_val05 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Common-block subroutine: write value 0x05 to whichever external-chip
 * register the caller already staged in IRAM 0x2E.
 *
 * The two call sites are 0x088F (after `MOV 0x2E,#5`) and 0x0895 (after
 * `MOV 0x2E,#6`), so the effect is register 5 = 0x05 and register 6 = 0x05.
 * The block Keil found in common was `value = 5; write(reg, value)` -- the
 * value store is inside the helper, the register store is not, because the
 * two sites differ there.
 *
 * The register numbers (5 and 6) and the value (0x05) are read from the bytes
 * and are certain, AND SO IS THE NAMING. The part is a Cirrus Logic CS8427
 * (firmware_stock/decomp/FINDING_cs8427_confirmed.md); ALSA's CS8427 header
 * names 0x05 CS8427_REG_SERIALINPUT and 0x06 CS8427_REG_SERIALOUTPUT -- the
 * serial audio input and output port format registers. Writing the same byte
 * to both configures the two sides of the serial audio port identically, which
 * is what this helper exists to do.
 *
 * 0x05 = 0000 0101 decodes, bit by bit, identically on both registers (the
 * two registers' bit layouts are mirror images, SI* against SO*):
 *
 *     bit 7  SIMS / SOMS   = 0   SLAVE mode -- the TAS1020B drives ILRCK/
 *                                ISCLK and OLRCK/OSCLK, the CS8427 follows
 *     bit 6  SISF / SOSF   = 0   64*Fs bit clock
 *     bits 5:4  SIRESMASK / SORESMASK
 *                          = 00  CS8427_SIRES24 / CS8427_SORES24 -- 24-BIT
 *     bit 3  SIJUST/SOJUST = 0   LEFT-justified
 *     bit 2  SIDEL / SODEL = 1   data delayed to the SECOND clock period
 *                                after the LRCK edge
 *     bit 1  SISPOL/SOSPOL = 0   data clocked on the RISING edge
 *     bit 0  SILRPOL/SOLRPOL = 1 LRCK polarity inverted -- data is the RIGHT
 *                                channel while LRCK is high
 *
 * Left-justified, plus a one-clock delay, plus inverted LRCK polarity is
 * exactly I2S. So both directions are 24-bit I2S with the CS8427 as clock
 * slave -- consistent with the 24-bit format the stock descriptors advertise.
 * (ALSA's header is a secondary source: cite it as "ALSA's CS8427 header names
 * this ...", not as datasheet text.)
 *
 * REV 22 CROSS-CHECK: this helper does not exist in rev22. Rev 22's
 * audio_hw_bringup (0x09B6) inlines the same ten (register, value) pairs at
 * 0x09F8-0x0A3D, and the two writes this helper serves appear there as
 * `MOV R7,#0x5 / MOV R5,#0x5 / LCALL 0x0C31` at 0x0A29 and
 * `MOV R7,#0x6 / MOV R5,#0x5 / LCALL 0x0C31` at 0x0A30 -- same registers,
 * same value, same position in the sequence. Only the common-block factoring
 * differs between the images.
 *
 * __naked: common-block extraction plus the R7/R5 register-parameter
 * convention of cs8427_ctl_write.
 */
void extchip_write_val05(void) __naked {
    __asm
        .globl _cs8427_ctl_write
        mov   0x2f,#0x05       ; staged value = 5 (IRAM byte 0x2F, not bit 0x2F)
        mov   r5,0x2f
        mov   r7,0x2e          ; register staged by the caller
        ljmp  _cs8427_ctl_write ; 0x0C45
    __endasm;
}
