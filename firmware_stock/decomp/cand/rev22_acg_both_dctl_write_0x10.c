// MATCH: image=rev22 addr=0x0EF4 len=8 func=acg_both_dctl_write_0x10 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Set both adaptive clock generator divider-control registers to 0x10.
 *
 * The register written first is NOT named here: it is whatever DPTR points at
 * on entry, so DPTR is an input parameter and this cannot be C.  Two routes
 * in, both ending at ACG1DCTL (0xFFE2):
 *
 *   - audio_clock_set_mode loads `MOV DPTR,#0xFFE2` and LCALLs 0x0EF4
 *     directly (rev22 0x071D / 0x0720).
 *   - hw_clock_codec_init (0x0852) and audio_path_reconfig (0x09C7) arrive
 *     from the ACG programming block, which leaves DPTR on ACGCTL (0xFFE1),
 *     so they enter one byte earlier at 0x0EF3 (Ghidra: acg_dividers_div2)
 *     where a single `INC DPTR` steps 0xFFE1 -> 0xFFE2.
 *
 * The second write is unconditional: DPTR is reloaded with ACG2DCTL (0xFFF6)
 * and the same A stored, so both synthesizers get the same divider setting in
 * one call -- Keil keeping A live across a DPTR reload.
 *
 * Value 0x10 is bit 4 of ACGxDCTL.  The only place in either image that writes
 * one of these registers anything else is mode 5 of the clock-mode setter,
 * which puts 0x00 in ACG1DCTL alone.
 *
 * ---------------------------------------------------------------------------
 * REV 20 -> REV 22 DELTA: none.  rev20 0x0E18 and rev22 0x0EF4 are the same
 * eight bytes, `74 10 f0 90 ff f6 f0 22`, and the INC DPTR prologue sits at
 * rev20 0x0E17 / rev22 0x0EF3 in both.  Verified by direct byte comparison.
 */
void acg_both_dctl_write_0x10(void) __naked {
    __asm
        mov   a,#0x10
        movx  @dptr,a          ; caller's register: ACG1DCTL (0xFFE2)
        mov   dptr,#0xfff6     ; ACG2DCTL
        movx  @dptr,a          ; same A, still live across the DPTR reload
        ret
    __endasm;
}
