// MATCH: image=rev22 addr=0x044E len=12 entry=1 func=evt_arm_zlp_and_finish cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void evt_dispatch_epilogue(void);   /* rev22 0x0563, rev20 0x0564 */

/* THE REV 22 SHARED EVENT TAIL — the single biggest structural difference
 * between the two images in this region, and a pure code-size change.
 *
 * Rev 20 gave every event handler its own two-instruction epilogue:
 *
 *     LCALL 0x0FEA     ; ep0_arm_zlp: IEPDCNTX0 = 0; OEPDCNTX0 = 0
 *     LJMP  0x0564     ; evt_dispatch_epilogue: g_event = 0
 *
 * six bytes, written out at rev20 0x0380 (cmd1), 0x03F7 (cmd2) and 0x044E
 * (cmd3). Rev 22 kept exactly one copy, INLINED the ep0_arm_zlp body into it,
 * and has cmd1/cmd2/cmd3 branch to it: rev22 0x044E is reached from six sites
 * (0x0369, 0x0387, 0x03FB, 0x0430, 0x0446, 0x0448). Rev 22 has no standalone
 * ep0_arm_zlp function at all — the 0x0FEA slot in Rev 20's helper block is
 * occupied in Rev 22 by cport_cnf3_write_enable's tail instead.
 *
 * Net effect on the three handlers' Ghidra extents: cmd1 84 B vs Rev 20's 92,
 * cmd2 115 vs 119, cmd3 93 vs 87. cmd3 grew because Ghidra hands it the twelve
 * shared bytes; the other two shrank by giving them up. Nothing about what the
 * device does changed.
 *
 * What the tail does, in protocol terms: writing 0 to IEPDCNTX0 (0xFF6B) and
 * OEPDCNTX0 (0xFFAB) arms EP0's IN and OUT X-buffers with a zero-byte count,
 * i.e. hands the USB engine a zero-length packet in each direction. For a
 * SET_INTERFACE / vendor SET the IN ZLP is the status stage; arming OUT at the
 * same time readies the next SETUP's data phase. Then g_event (IRAM 0x0A) is
 * cleared so the main loop stops re-dispatching this event.
 *
 * entry=1: these twelve bytes physically live at the end of Ghidra's
 * cmd3_apply_iface2_alt (0x03FD..0x0459), so rev22_cmd3_apply_iface2_alt.c
 * places them. This candidate exists to prove the bytes standalone and to give
 * link51 an equate cmd1 and cmd2 can jump to.
 *
 * NOTE the CLR A is shared between the two stores — Keil loads zero once and
 * reuses it for both MOVX. That is the `mov dir,#0` -> `clr a` peephole plus
 * SDCC's own reuse; the same C matches Rev 20's ep0_arm_zlp at 0x0FEA. */
void evt_arm_zlp_and_finish(void) {
    IEPDCNTX0 = 0;               /* EP0 IN  X-buffer: zero-length packet  */
    OEPDCNTX0 = 0;               /* EP0 OUT X-buffer: zero-length packet  */
    evt_dispatch_epilogue();     /* tail call -> LJMP 0x0563; g_event = 0 */
}
