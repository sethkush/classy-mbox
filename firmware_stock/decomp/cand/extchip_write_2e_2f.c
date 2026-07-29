// MATCH: image=rev20 addr=0x08BD len=7 func=extchip_write_2e_2f cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Common-block subroutine: emit the (register, value) pair staged in IRAM
 * 0x2E:0x2F to the external serial audio chip.
 *
 * This was never a function in the source. Keil's optimiser extracts repeated
 * identical instruction blocks into subroutines ("common block subroutines"),
 * and the Rev 20 build of audio_path_reconfig_ext_chips (0x080B) contains ten
 * copies of the three-instruction sequence
 *
 *     MOV R5,0x2F  /  MOV R7,0x2E  /  {L}CALL cs8427_ctl_write
 *
 * because the source is ten straight-line calls of the form write(reg, val).
 * The compiler allocated the two arguments to IRAM 0x2E and 0x2F, then pulled
 * the argument-shuffle-and-call out into helpers. Two of the ten call sites
 * (0x085E and 0x086A) reach this copy.
 *
 * The proof that this is a build artefact and not source structure is Rev 22:
 * the same source compiles there with the arguments in R7/R5 directly and the
 * calls fully inlined -- rev22 audio_hw_bringup at 0x09B6 has ten literal
 * `MOV R7,#reg / MOV R5,#val / LCALL 0x0C31` groups and no helper at all.
 * (Rev 22 does keep two similar helpers, cs8427_write_reg04_val41 at 0x0567
 * and cs8427_write_shadowed at 0x0575, but those serve the command handlers,
 * not the bring-up path, and stage through IRAM 0x2C:0x2D instead.)
 *
 * Written __naked for two independent reasons: SDCC has no equivalent of
 * Keil's common-block extraction, and the callee takes its arguments in R7
 * (register) and R5 (value), which is Keil's register-parameter convention
 * and not SDCC's.
 *
 * The trailing LJMP rather than LCALL+RET is an ordinary tail call.
 */
void extchip_write_2e_2f(void) __naked {
    __asm
        .globl _cs8427_ctl_write
        mov   r5,0x2f          ; value    -> R5   (IRAM byte 0x2F, not bit 0x2F)
        mov   r7,0x2e          ; register -> R7   (IRAM byte 0x2E, not bit 0x2E)
        ljmp  _cs8427_ctl_write ; 0x0C45: 3-byte bit-banged write {0x20, reg, val}
    __endasm;
}
