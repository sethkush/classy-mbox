// MATCH: image=rev22 addr=0x0EF3 len=1 func=acg_dividers_div2 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* One byte: INC DPTR (0xA3), falling through into acg_both_dctl_write_0x10 at
 * 0x0EF4 (cand/rev22_acg_both_dctl_write_0x10.c).
 *
 * A merged entry point, not a function. DPTR is the input and there is no RET;
 * the RET at 0x0EFB serves both entries.
 *
 * Its whole purpose is to step DPTR from ACGCTL (0xFFE1) to ACG1DCTL (0xFFE2).
 * Its two callers -- hw_clock_codec_init at 0x0852 and audio_path_reconfig at
 * 0x09C7 -- have just returned from the ACG programming block, whose tail
 * (0x0EEC..0x0EF2) leaves DPTR on ACGCTL. Rather than reload a known constant,
 * Keil increments the pointer that is already live across the call. That is the
 * inter-procedural register knowledge the README calls out as the thing SDCC
 * cannot reproduce; here it costs exactly one byte and is expressible in
 * assembly, so it needs no partial.
 *
 * The caller that arrives with DPTR set to ACG1DCTL directly
 * (audio_clock_set_mode, MOV DPTR,#0xFFE2 at 0x071D then LCALL at 0x0720)
 * enters one byte later at 0x0EF4 and skips the increment.
 *
 * Ghidra's name for this byte, acg_dividers_div2, reads a meaning into it that
 * the byte does not carry -- INC DPTR is pointer arithmetic, not a divider
 * setting. The divider value 0x10 comes from the routine it falls into. The
 * name is kept only because the header must match the listing.
 *
 * REV 20 -> REV 22 DELTA: byte-identical, relocated from rev20 0x0E17 to rev22
 * 0x0EF3. Verified directly: rev20 0x0E17..0x0E1F and rev22 0x0EF3..0x0EFB are
 * both `a3 74 10 f0 90 ff f6 f0 22`, i.e. this prologue and the whole routine
 * behind it are the same nine bytes in both images.
 *
 * Rev 20's copy is one of the two single-byte prologues the README lists as
 * uncovered there (0x0DEB and 0x0E17), for a tooling reason: link51 takes a
 * candidate's defined symbols from the header's func= name, so a candidate
 * cannot declare a second entry label, and rev20 0x0E17 falls inside a placed
 * function. In Rev 22 the neighbouring candidate starts at 0x0EF4, so this byte
 * is outside every other extent and can be claimed on its own. A symbols.map
 * row is proposed in proposed/stubs.symbols anyway, since it is a genuine
 * entry point. */
void acg_dividers_div2(void) __naked {
    __asm
        inc   dptr              ; ACGCTL (0xFFE1) -> ACG1DCTL (0xFFE2), then
                                ; fall into acg_both_dctl_write_0x10 at 0x0EF4
    __endasm;
}
