// MATCH: image=rev22 addr=0x0259 len=66 func=std_set_configuration cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void ep0_set_both_dcnt(void);    /* rev22 0x0B2E, takes the count in A */
extern void ep0_stall_both(void);       /* rev22 0x02EF */

/* Bit addresses -- bit B is IRAM 0x20+(B>>3), bit B&7, NOT IRAM byte B. */
__bit __at (0x08) f_iface1_alt;   /* IRAM 0x21.0 */
__bit __at (0x09) f_iface2_alt;   /* IRAM 0x21.1 */
__bit __at (0x0A) f_cfg_alt;      /* IRAM 0x21.2 -- written here, never set */
__bit __at (0x0E) f_configured;   /* IRAM 0x21.6 */

/* SET_CONFIGURATION, Rev 22 (rev22 0x0259, 66 B; rev20 0x025B, 62 B).
 *
 * BEHAVIOUR IS IDENTICAL TO REV 20, instruction for instruction, for the
 * first 59 bytes: accept configuration 0 or 1, reset both interfaces to
 * alternate setting 0, record whether the device is now configured, queue
 * event 1 (re-apply the whole audio path) and NAK EP0 until it has run.
 *
 * The wValue == 2 arm is dead in both images -- the `SETB C / SUBB A,#1 / JC`
 * guard above rejects anything >= 2 -- but Keil emitted it, so it is
 * reproduced.  (Contrast std_get_interface, whose third arm IS reachable
 * because its guard uses #2 with the borrow already set.  Same-looking idiom,
 * different constant, different reachability; do not generalise from one to
 * the other.)
 *
 * The three arms are written as three independent `if`s, not if/else-if,
 * because that is what stock does: each one re-reads SETUP_wValueL from
 * 0xFF2A rather than keeping it in a register.
 *
 * REV 20 -> REV 22 DELTA (encoding only; +4 bytes, 62 -> 66).  Only the exit
 * tail changed:
 *   rev20 0x0296  LJMP 0x0B5F (ep0_nack_both)                     3 B
 *   rev22 0x0294  MOV A,#0x80 / LCALL 0x0B2E / SJMP 0x02E8        7 B
 * Rev 20's ep0_nack_both was
 *   MOV DPTR,#0xFF6B / MOV A,#0x80 / MOVX / MOV DPTR,#0xFFAB / MOVX
 *   / CLR 0x0B / CLR 0x0C / RET.
 * Rev 22 cut it in two: ep0_set_both_dcnt (0x0B2E) writes A to IEPDCNTX0
 * (0xFF6B) and OEPDCNTX0 (0xFFAB) for any count, and ep0_done_no_data
 * (0x02E8) is the `CLR 0x0B / CLR 0x0C / RET` tail now shared by four
 * request handlers.  0x80 is the NAK bit, so the effect on the wire is
 * unchanged: EP0 NAKs in both directions until the deferred action runs.
 * (std_set_interface, by contrast, inlined the two NAK writes rather than
 * calling ep0_set_both_dcnt -- Rev 22 made that choice per site.)
 *
 * WHY THE TAIL IS INLINE ASSEMBLY.  Two things SDCC cannot spell:
 * ep0_set_both_dcnt takes its argument in A (Keil's convention for a helper
 * that is really a code fragment; SDCC would pass it in DPL), and the exit is
 * a two-byte SJMP into another function, which SDCC never emits for an
 * external symbol.  The function is therefore `__naked` so no RET is appended
 * after the SJMP -- everything above the tail is ordinary C and matches stock
 * byte for byte on its own.  The displacement is written `.`-relative so it
 * is correct wherever the function is placed.
 */
void std_set_configuration(void) __naked {
    /* Written as inline asm rather than `{ ep0_stall_both(); return; }`
     * because in a __naked function SDCC cannot end a path with RET, so it
     * turns the C call into LCALL + a jump to the (absent) epilogue. */
    if (SETUP_wValueL >= 2) {
        __asm
            .globl _ep0_stall_both
            ljmp  _ep0_stall_both
        __endasm;
    }
    if (SETUP_wValueL == 0) {
        f_cfg_alt = 0; f_configured = 0; f_iface1_alt = 0; f_iface2_alt = 0;
    }
    if (SETUP_wValueL == 1) {
        f_cfg_alt = 0; f_configured = 1; f_iface1_alt = 0; f_iface2_alt = 0;
    }
    if (SETUP_wValueL == 2) {   /* dead: rejected by the guard above */
        f_cfg_alt = 0; f_configured = 1; f_iface1_alt = 0; f_iface2_alt = 0;
    }
    g_event = 1;                /* re-apply configuration/audio path */
    __asm
        .globl _ep0_set_both_dcnt
        mov   a,#0x80             ; NAK both directions of EP0
        lcall _ep0_set_both_dcnt  ; IEPDCNTX0 = OEPDCNTX0 = A
        sjmp  .+0x4f              ; -> ep0_done_no_data at 0x02E8
    __endasm;
}
