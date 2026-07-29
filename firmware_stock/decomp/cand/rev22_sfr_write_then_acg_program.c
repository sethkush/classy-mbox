// MATCH: image=rev22 addr=0x0EC7 len=1 func=sfr_write_then_acg_program cflags=--peep-file,firmware_stock/decomp/keil.peep
/* One byte: MOVX @DPTR,A (0xF0), falling through into
 * acg_both_synths_24576khz at 0x0EC8 (cand/rev22_acg_both_synths_24576khz.c).
 *
 * It is a merged entry point, not a function: "commit the byte in A to the SFR
 * DPTR already points at, then go program both ACG synthesizers". Both DPTR
 * and A are inputs, which is why it is assembly and why it has no RET -- the
 * RET that serves it belongs to the routine it falls into.
 *
 * Keil emitted it because two callers each finish a read-modify-write and then
 * immediately want the ACG block, and folding the store into the entry saves
 * them a byte apiece:
 *
 *   0x078A  audio_clock_set_mode: DPTR = GLOBCTL (0xFFB1), A = GLOBCTL | 0x01.
 *           So the store enables the ACG clock and the fall-through programs
 *           the synthesizers it just enabled -- the ordering is the point.
 *   0x084F  hw_clock_codec_init: DPTR = 0xFFD4, A = 0x03.
 *
 * Callers that do NOT need a store enter one byte later at 0x0EC8 (0x076F,
 * 0x09C4).
 *
 * This also demonstrates the DPTR liveness the ACG block relies on: control
 * returns to 0x078D and 0x0852 with DPTR still on ACGCTL (0xFFE1), left there
 * by the tail at 0x0EEC, and both sites use it immediately -- 0x078D does
 * INC DPTR / CLR A / MOVX to zero ACG1DCTL, and 0x0852 calls 0x0EF3, whose
 * whole body is that same INC DPTR (cand/rev22_acg_dividers_div2.c).
 *
 * REV 20 -> REV 22 DELTA: byte-identical, relocated from rev20 0x0DEB (Ghidra
 * there names it sfr_store_then_cpt_cfg_tail) to rev22 0x0EC7. Both are 0xF0
 * and both are the one-byte prologue of the same ACG programming block --
 * rev20 0x0DEB..0x0E0B and rev22 0x0EC7..0x0EE7 agree byte for byte over the
 * first 0x1F bytes. (The two blocks do diverge slightly further along, at
 * rev20 0x0E0A / rev22 0x0EE6, but that is inside another batch's function and
 * I have not decoded it, so I make no claim about it here.)
 *
 * The Rev 20 batch could not cover its copy: link51 derives a candidate's
 * defined symbols from the header's func= name alone, so a candidate cannot
 * declare a second entry label, and rev20 0x0DEB falls inside a placed
 * function's bytes. Rev 22's layout does not have that problem -- 0x0EC7 is
 * outside every other candidate's extent -- so it can be claimed directly as
 * its own one-byte candidate. A symbols.map row is proposed in
 * proposed/stubs.symbols regardless, since it is a real entry point. */
void sfr_write_then_acg_program(void) __naked {
    __asm
        movx  @dptr,a           ; commit caller's A to caller's SFR, then fall
                                ; into acg_both_synths_24576khz at 0x0EC8
    __endasm;
}
