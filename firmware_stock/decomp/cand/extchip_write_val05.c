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
 * and are certain. The naming is not: IF the part is a CS8427, then registers
 * 5 and 6 are its serial input and serial output format registers, and writing
 * the same byte to both would configure the two sides of the serial audio port
 * identically. No CS8427 datasheet exists anywhere under reference/ -- see
 * extchip_write_reg4_zero.c (0x08A6) for the provenance of the chip
 * identification and why it is still rated only "likely". Nothing here settles
 * it either; what the bytes show is two writes of 0x05 to two adjacent
 * registers.
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
