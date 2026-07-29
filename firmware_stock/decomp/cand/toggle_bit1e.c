// MATCH: image=rev20 addr=0x1028 len=9 func=toggle_bit1e cflags=--peep-file,firmware_stock/decomp/keil.peep
/* IRAM 0x23 bit 6 = bit address 0x1E. Toggled by the P3.5 front-panel button.
 * Written with an explicit early return: Keil emitted the naive
 * JNB / CLR / RET / SETB / RET form rather than folding to CPL. */
__bit __at (0x1E) g_panel_bit1e;
void toggle_bit1e(void) {
    if (g_panel_bit1e) { g_panel_bit1e = 0; return; }
    g_panel_bit1e = 1;
}
