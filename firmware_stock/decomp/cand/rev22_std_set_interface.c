// MATCH: image=rev22 addr=0x029D len=75 func=std_set_interface cflags=--peep-file,firmware_stock/decomp/keil.peep

/* SET_INTERFACE, Rev 22 (rev22 0x029D, 75 B; rev20 0x029F, 72 B).
 *
 * BEHAVIOUR IS UNCHANGED FROM REV 20.  Instruction for instruction the two
 * bodies are the same up to the exit tails: same three guards (wIndex > 2,
 * wValue >= 2, "not configured"), same two arms recording the alternate
 * setting as one bit per interface, same queued event codes 2 and 3.  Only
 * the exits differ, and only in encoding -- see the delta note at the bottom.
 *
 * WHY THIS IS ASSEMBLY AND NOT C.  Three of the 75 bytes are branches whose
 * targets lie OUTSIDE the function:
 *
 *   * the three guards all branch to 0x02ED, a two-byte `SJMP ep0_stall_both`
 *     branch island that sits five bytes past the end of this function (it is
 *     claimed, with an explanation, by cand/rev22_ep0_done_no_data.c which
 *     spans it).  A one-byte displacement from 0x02A4 cannot reach 0x02EF
 *     directly, so Keil parked a trampoline inside reach;
 *   * the function does not end in RET at all.  It runs off the end of its
 *     own last instruction (0x02E7) straight into ep0_done_no_data at 0x02E8.
 *
 * Neither a relative branch to an address beyond the function nor a
 * fall-through into the next function is expressible in C, and SDCC will not
 * emit a short jump to an external symbol in any case (README, "Function
 * ordering").  The displacements are therefore written as `.`-relative
 * expressions so they are correct wherever the function is placed.
 *
 * SFRs written, from the TAS1020B USB buffer manager:
 *   0xFF6B  IEPDCNTX0   input  endpoint 0 data count / NAK control
 *   0xFFAB  OEPDCNTX0   output endpoint 0 data count / NAK control
 * Writing 0x80 to both sets the NAK bit: EP0 is told to hold off the host
 * until the deferred action queued in g_event has been carried out.
 *
 * IRAM state (bit addresses -- bit B is IRAM 0x20+(B>>3), bit B&7):
 *   bit 0x08  f_iface1_alt   IRAM 0x21.0   interface 1 is on a non-zero alt
 *   bit 0x09  f_iface2_alt   IRAM 0x21.1   interface 2 is on a non-zero alt
 *   bit 0x0A  f_cfg_alt      IRAM 0x21.2   written by SET_CONFIGURATION, never set
 *   bit 0x0E  f_configured   IRAM 0x21.6   a non-zero configuration is selected
 * BYTE 0x0A is a different location entirely: g_event, the deferred-action
 * code picked up by usb_deferred_action_dispatch (rev22 0x02F3).  `JB 0x0a`
 * below tests the bit; `MOV 0x0a,#2` writes the byte.
 *
 * REV 20 -> REV 22 DELTA (encoding only, 3 bytes longer):
 *   rev20 0x02DE  LJMP 0x1009 (ep0_stall_both)      3 B  <- the "wIndex is
 *                 neither 1 nor 2" arm, which the wIndex > 2 guard already
 *                 made unreachable; still present in Rev 22
 *   rev22 0x02DC  SJMP 0x02EF (ep0_stall_both)      2 B
 *   rev20 0x02E1  LJMP 0x0B5F (ep0_nack_both)       3 B
 *   rev22 0x02DE  the first five instructions of that helper INLINED (10 B),
 *                 falling through into ep0_done_no_data (0x02E8), which is
 *                 the helper's own tail factored out for four other callers
 *   rev20 0x02E4  LJMP 0x1009, the shared guard exit  3 B
 *   rev22         guards branch to the 0x02ED island instead, 0 B in-function
 * Net 72 -> 75.  Rev 20's ep0_nack_both (0x0B5F) has no Rev 22 counterpart as
 * a whole function: Rev 22 split it, inlining the two NAK writes here and
 * keeping only `CLR 0x0B / CLR 0x0C / RET` as ep0_done_no_data.
 */
void std_set_interface(void) __naked {
    __asm
        ;; ---- guard 1: wIndex must be 0, 1 or 2 --------------------------
        mov   dptr,#0xff2c        ; SETUP_wIndexL
        movx  a,@dptr
        setb  c
        subb  a,#0x02             ; borrow iff wIndex < 2 ... see note
        jnc   .+0x49              ; -> 0x02ED island -> ep0_stall_both
        ;; SETB C / SUBB A,#2 borrows when A < 3 once the borrow-in is counted,
        ;; so wIndex 0, 1 and 2 all fall through and only >2 is rejected.

        ;; ---- guard 2: alternate setting must be 0 or 1 ------------------
        ;; carry is still set from the SUBB above, which is why this SUBB uses
        ;; #1 and not #2 -- Keil folded the SETB C of the second comparison
        ;; into the first.  Effective test: wValue >= 2 rejects.
        mov   dptr,#0xff2a        ; SETUP_wValueL (bAlternateSetting)
        movx  a,@dptr
        subb  a,#0x01
        jnc   .+0x41              ; -> 0x02ED island -> ep0_stall_both

        ;; ---- guard 3: must be configured -------------------------------
        jb    0x0a,0001$          ; f_cfg_alt (vestigial: never set anywhere)
        jnb   0x0e,.+0x3c         ; not configured -> stall
    0001$:

        ;; ---- interface 1 -----------------------------------------------
        mov   dptr,#0xff2c
        movx  a,@dptr
        cjne  a,#0x01,0002$
        mov   dptr,#0xff2a
        movx  a,@dptr
        add   a,#0xff             ; carry = (wValue != 0)
        mov   0x08,c              ; f_iface1_alt = alt != 0
        mov   0x0a,#0x02          ; g_event = 2: re-apply interface 1 alt
        sjmp  0003$

        ;; ---- interface 2 -----------------------------------------------
    0002$:
        mov   dptr,#0xff2c
        movx  a,@dptr
        cjne  a,#0x02,0004$
        mov   dptr,#0xff2a
        movx  a,@dptr
        add   a,#0xff
        mov   0x09,c              ; f_iface2_alt = alt != 0
        mov   0x0a,#0x03          ; g_event = 3: re-apply interface 2 alt
        sjmp  0003$

        ;; ---- unreachable else arm (wIndex 0 was not rejected by guard 1,
        ;; but interface 0 has no alternate settings and falls here) -------
    0004$:
        sjmp  .+0x13              ; -> ep0_stall_both at 0x02EF

        ;; ---- accepted: NAK EP0 in both directions, then fall through
        ;; into ep0_done_no_data (0x02E8) for CLR 0x0B / CLR 0x0C / RET ----
    0003$:
        mov   dptr,#0xff6b        ; IEPDCNTX0
        mov   a,#0x80             ; NAK
        movx  @dptr,a
        mov   dptr,#0xffab        ; OEPDCNTX0
        movx  @dptr,a
        ;; no RET here -- execution continues at 0x02E8
    __endasm;
}
