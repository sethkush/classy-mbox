// MATCH: image=rev22 addr=0x0247 len=6 func=ep0_arm_in_and_done cflags=--peep-file,firmware_stock/decomp/keil.peep

/* The common tail of every EP0 IN reply that fits in one packet: commit the
 * byte count and declare an IN data stage armed.
 *
 * Called with DPTR already pointing at IEPDCNTX0 (0xFF6B) and A holding the
 * byte count, which is why it starts with a bare MOVX.  Three callers, each
 * supplying a different count:
 *     rev22 0x0103  ep0_arm_in_3bytes      A = 3  (class GET_CUR sample rate)
 *     rev22 0x0174  std_get_configuration  A = 1
 *     rev22 0x022A  std_get_interface      A = 1
 * plus std_get_status at rev22 0x0242, which sets A = 2 and falls straight
 * through into it rather than jumping.
 *
 * Then f_stage_out = 0 / f_stage_in = 1: the next thing expected on EP0 is the
 * IN data stage this arms, not an OUT.
 *
 * REV 20 -> REV 22: NEW FUNCTION, no counterpart.  Rev 20 had ep0_send_1byte
 * at 0x0B45 (IEPDCNTX0 = 1 / clr / setb, 11 B) which hard-coded the count of
 * one, and the three-byte case open-coded its own copy at
 * send_3byte_ep0_reply, rev20 0x010D.  Rev 22 factored the count out to the
 * caller so one 6-byte tail serves all counts.  Behaviour is unchanged; it is
 * a size refactor.
 *
 * WRITTEN AS ASSEMBLY: both of its inputs arrive in registers (A and DPTR),
 * which is not a calling convention C can express -- the same situation as
 * cand/ep0_buf_clear_byte.c in Rev 20.
 */
void ep0_arm_in_and_done(void) __naked {
    __asm
        movx  @dptr,a              ; IEPDCNTX0 = A: byte count for the IN stage
        clr   0x0b                 ; f_stage_out = 0 (IRAM 0x21.3)
        setb  0x0c                 ; f_stage_in  = 1 (IRAM 0x21.4)
        ret
    __endasm;
}
