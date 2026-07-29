// MATCH: image=rev20 addr=0x0E18 len=8 func=acg_set_both_dctl_10 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Set both adaptive clock generator divider-control registers to 0x10.
 *
 * The register written first is *not* named here: it is whatever DPTR points
 * at on entry. Every caller points it at ACG1DCTL (0xFFE2), by one of two
 * routes:
 *
 *   - audio_clock_mode_apply loads `MOV DPTR,#0xFFE2` and LCALLs 0x0E18
 *     directly (rev20 0x0736/0x0739; rev22 0x071D/0x0720).
 *   - hw_master_init (0x0931) and audio_path_reconfig_ext_chips (0x081E)
 *     arrive from acg_48k_commit, whose last write leaves DPTR on ACGCTL
 *     (0xFFE1; loaded rev20 0x0E10, stored 0x0E15), so they enter one byte
 *     earlier at 0x0E17 where
 *     a single INC DPTR steps 0xFFE1 -> 0xFFE2.
 *
 * The second write is unconditional: DPTR is reloaded with ACG2DCTL (0xFFF6)
 * and the same A is stored, so both synthesizers get the same divider setting
 * in the same call. A is loaded once and used for both stores -- Keil keeping
 * a value live across a DPTR reload, which is why this is __naked: DPTR is an
 * input parameter of the function, and "the caller's DPTR" is not expressible
 * in C at all.
 *
 * 0x0E17 is therefore an alternate entry point into these bytes, not a
 * separate function, and is carried in firmware_stock/decomp/symbols.map as
 * `acg_incdptr_dctl_div2 = 0x0E17` rather than as a candidate of its own. Its
 * one byte (0xA3) is not covered by any placed candidate -- the same treatment
 * already given to `sfr_store_then_acg_48k = 0x0DEB` and
 * `sfr_store_then_acg_ctl6 = 0x0E0F`. See the report note: a one-byte prologue
 * cannot be spanned into this candidate, because link51 derives its
 * already-defined set from the header's `func=` name alone and would then
 * emit a duplicate equate for the second label.
 *
 * Value 0x10: bit 4 of ACGxDCTL. The two images agree exactly -- rev20 0x0E18
 * and rev22 0x0EF4 are the identical eight bytes
 * `74 10 f0 90 ff f6 f0 22`, and the INC DPTR prologue is at rev20 0x0E17 /
 * rev22 0x0EF3. Callers of the 0x0E17 form are rev20 0x081E and 0x0931,
 * rev22 0x0852 and 0x09C7.
 */
void acg_set_both_dctl_10(void) __naked {
    __asm
        mov   a,#0x10
        movx  @dptr,a          ; caller's register: ACG1DCTL (0xFFE2)
        mov   dptr,#0xfff6     ; ACG2DCTL
        movx  @dptr,a          ; same A, still live across the DPTR reload
        ret
    __endasm;
}
