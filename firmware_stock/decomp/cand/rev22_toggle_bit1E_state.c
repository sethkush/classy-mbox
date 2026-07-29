// MATCH: image=rev22 addr=0x1020 len=9 func=toggle_bit1E_state cflags=--peep-file,firmware_stock/decomp/keil.peep
/* IRAM 0x23 bit 6 = bit address 0x1E.  Toggled once per qualifying edge on the
 * P3.5 front-panel button, called from p3_edge_poll_dispatch (rev22 0x0F41).
 *
 * Its only consumer is the tail of shiftreg_out8_p1hi (rev22 0x0F20,
 * `JNB 0x1E,0x0F27`), where it selects between a normal P1.6 strobe pulse and
 * driving P1.7/P1.6 both high and leaving them there -- i.e. it is a
 * strobe-mode toggle for the panel shift-register chain.  It is NOT 48 V
 * phantom power; an older note in this project claimed that and nothing in
 * either image supports it.
 *
 * Note IRAM 0x23 is also chain B's first shift-out payload byte
 * (shiftreg_out16_p1, rev22 0x0E58 `MOV R7,0x23`), so this bit is
 * simultaneously clocked out by that routine.  Same storage, two uses.
 *
 * Written with an explicit early return because Keil emitted the naive
 * JNB / CLR / RET / SETB / RET rather than folding to CPL 0x1E.  SDCC folds it
 * to JBC unless steered; the keil.peep rule that rewrites
 * jbc/sjmp/label/ret back into jnb/clr/ret is what makes this match.
 *
 * REV 20 -> REV 22 DELTA: none.  All nine stock bytes at rev22 0x1020 are
 * identical to rev20 0x1028 (toggle_bit1e): 30 1e 03 c2 1e 22 d2 1e 22.  Pure
 * bit-addressed code with one relative branch, so nothing to relocate.
 */
__bit __at (0x1E) g_panel_bit1e;
void toggle_bit1E_state(void) {
    if (g_panel_bit1e) { g_panel_bit1e = 0; return; }
    g_panel_bit1e = 1;
}
