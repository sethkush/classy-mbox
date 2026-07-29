// MATCH: image=rev20 addr=0x08C4 len=7 func=extchip_write_2e_2f_dup cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Byte-for-byte identical twin of extchip_write_2e_2f (0x08BD). Both are
 * `MOV R5,0x2F / MOV R7,0x2E / LJMP cs8427_ctl_write`; stock has the same
 * seven bytes at 0x08BD and at 0x08C4 (verified against
 * firmware_stock/rev20_firmware_code.bin: 0x08BD and 0x08C4 both read
 * ad 2f af 2e 02 0c 45).
 *
 * That duplication is the clearest evidence available that these helpers are
 * a code-generator artefact rather than source functions. A programmer writing
 * two identical three-line helpers, and a compiler doing whole-image common
 * subexpression elimination, would both have produced one. What Keil actually
 * does is scan for repeated instruction blocks within a limited window and
 * emit a subroutine per group it finds; the ten call sites in
 * audio_path_reconfig_ext_chips (0x080B) fell into two such groups, so two
 * identical subroutines came out.
 *
 * This copy serves the calls at 0x0873 and 0x087C -- the writes of
 * register 1 = 0x01 and register 2 = 0x20. The 0x08BD copy serves 0x085E and
 * 0x086A. The split is positional, not semantic.
 *
 * Rev 22 has no counterpart to either: audio_hw_bringup (0x09B6) inlines all
 * ten writes as `MOV R7,#reg / MOV R5,#val / LCALL 0x0C31`.
 *
 * __naked for the same two reasons as its twin -- SDCC has no common-block
 * extraction, and the callee takes register parameters in R7/R5.
 */
void extchip_write_2e_2f_dup(void) __naked {
    __asm
        .globl _cs8427_ctl_write
        mov   r5,0x2f          ; value    -> R5   (IRAM byte 0x2F)
        mov   r7,0x2e          ; register -> R7   (IRAM byte 0x2E)
        ljmp  _cs8427_ctl_write ; 0x0C45
    __endasm;
}
