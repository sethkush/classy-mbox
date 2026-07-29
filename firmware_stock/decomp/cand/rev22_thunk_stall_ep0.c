// MATCH: image=rev22 addr=0x0100 len=3 func=thunk_stall_ep0 cflags=--peep-file,firmware_stock/decomp/keil.peep
extern void ep0_stall_both(void);   /* rev22 0x02EF */

/* A three-byte branch-range fixup: LJMP ep0_stall_both.
 *
 * One reference, from 0x00DA inside usb_setup_handler, and it is not a call --
 * it is the not-equal target of `CJNE A,#0x03,0x0100`. A CJNE carries an
 * 8-bit signed displacement, so from 0x00DD it can reach at most 0x015C; the
 * shared stall routine at 0x02EF is far outside that. Keil's answer is the
 * standard one: emit a long jump within reach and branch to it. It lands at
 * 0x0100 because that is the first byte past the end of usb_setup_handler
 * (0x0026..0x00FF), i.e. as close to the branch as it can be placed.
 *
 * So there is no source-level function here. In the C this is the `else` of
 * "if (bmRequestType-derived value == 3)" inside the SETUP handler, and it
 * stalls EP0. It is a separate candidate only because Rev 22's layout leaves it
 * outside any function's extent and Ghidra therefore names it.
 *
 * Expressed in C as a plain tail call: SDCC's tail-call peephole turns
 * LCALL/RET into LJMP, which is exactly the stock encoding. (Contrast
 * cand/rev22_ep0_stall_both.c, four bytes, where that same peephole is the
 * problem and the function must be written __naked.)
 *
 * REV 20 -> REV 22 DELTA: identical construct, relocated; no behavioural
 * change. Rev 20 has the same fixup at 0x010A -- `CJNE A,#0x03,0x010A` at
 * rev20 0x00E4 branching to `LJMP 0x1009` -- with the same shape (CJNE on the
 * same constant 3, target immediately past the SETUP handler, jumping to that
 * image's shared stall entry). The only differences are that Rev 20's stall
 * entry is 0x1009 rather than rev22's 0x02EF, and that Ghidra folds Rev 20's
 * copy into the surrounding function instead of naming it. */
void thunk_stall_ep0(void) {
    ep0_stall_both();
}
