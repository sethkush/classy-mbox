// MATCH: image=rev22 addr=0x02E8 len=7 span=1 func=ep0_done_no_data cflags=--peep-file,firmware_stock/decomp/keil.peep

/* "This request has no data stage": clear both stage flags and return, so the
 * EP0 state machine expects only the status stage.  Four callers, all of them
 * requests that are answered by acknowledgement alone:
 *     rev22 0x005E  class OUT interface, bRequest 0 (the DFU trigger)
 *     rev22 0x0156  std_clear_feature, accepted
 *     rev22 0x0256  std_set_address
 *     rev22 0x0299  std_set_configuration, accepted
 *
 * REV 20 -> REV 22: NEW FUNCTION.  Rev 20 open-coded these three instructions
 * at each of the corresponding sites (e.g. rev20 0x005E in
 * setup_class_out_interface, rev20 0x0257 in std_set_address).  Factoring them
 * out is why several Rev 22 handlers are two bytes shorter than their Rev 20
 * counterparts.  No behavioural change.
 *
 * THE SEVENTH BYTE IS NOT PART OF THE FUNCTION.  Ghidra's length covers
 * 0x02E8..0x02EE, and 0x02ED..0x02EE is `SJMP 0x02EF` with a displacement of
 * ZERO -- a jump to the very next instruction, which is the entry of
 * ep0_stall_both.  It is a branch island: std_set_interface has three
 * conditional branches (rev22 0x02A4, 0x02AC, 0x02B1) whose one-byte
 * displacements cannot reach 0x02EF from where they sit, so Keil parked a
 * two-byte SJMP inside reach and pointed them at it.  It belongs to
 * std_set_interface, not to this function, but it lives in this function's
 * byte range, so the candidate claims the whole run with span=1 and emits it
 * explicitly.  That is also why this is assembly rather than the obvious
 * two-statement C: the trailing island would have to follow the epilogue RET,
 * and SDCC has no way to place code there.
 */
void ep0_done_no_data(void) __naked {
    __asm
        clr   0x0b                 ; f_stage_out = 0 (IRAM 0x21.3)
        clr   0x0c                 ; f_stage_in  = 0 (IRAM 0x21.4)
        ret

    10$:                           ; 0x02ED -- branch island for std_set_interface
        sjmp  10$+2                ; displacement 0: falls onto ep0_stall_both
    __endasm;
}
