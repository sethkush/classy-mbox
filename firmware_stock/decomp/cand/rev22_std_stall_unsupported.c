// MATCH: image=rev22 addr=0x029B len=2 func=std_stall_unsupported cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Two bytes: SJMP ep0_stall_both. The shared "this standard request is not
 * supported" arm of the Rev 22 standard-request jump table.
 *
 * Three table slots LJMP here (Ghidra XREFs CODE:0127, CODE:0133, CODE:0142).
 * The table at 0x011E is indexed by bRequest * 3, so the slot addresses give
 * the request codes directly:
 *
 *     slot @0x0127 = (0x0127-0x011E)/3 = 3   SET_FEATURE
 *     slot @0x0133 = (0x0133-0x011E)/3 = 7   SET_DESCRIPTOR
 *     slot @0x0142 = (0x0142-0x011E)/3 = 12  SYNCH_FRAME
 *
 * All three are USB 2.0 table 9-4 requests this device declines to implement,
 * and it answers each with a protocol STALL on EP0.
 *
 * WRITTEN AS ASSEMBLY, and the SJMP is the whole reason. SDCC will not emit a
 * short jump to another function -- its peephole runs per function, so it never
 * sees the boundary -- and would give a three-byte LJMP here. As the README's
 * send_3byte_ep0_reply case records, an SJMP across a function boundary is
 * evidence about layout: 0x029B + 2 + 0x52 = 0x02EF, so ep0_stall_both is 0x52
 * bytes ahead and within short reach. The displacement is written numerically
 * because a symbolic short jump to an external symbol is not assemblable; the
 * arithmetic is spelled out so the target is checkable by hand.
 *
 * REV 20 -> REV 22 DELTA -- a real one, and the second-largest change in this
 * batch after the SOF vector. Rev 20 gives each of these three requests its own
 * three-byte stall arm:
 *
 *     rev20 std_set_feature_stall     0x029C  LJMP 0x1009   (bRequest 3)
 *     rev20 std_set_descriptor_stall  0x0299  LJMP 0x1009   (bRequest 7)
 *     rev20 std_synch_frame_stall     0x02E7  LJMP 0x1009   (bRequest 12)
 *
 * -- nine bytes, three functions, all doing the same thing. Rev 22 merges them
 * into these two bytes, a net saving of seven. Behaviour is unchanged: all
 * three requests still end at the same stall routine.
 *
 * The merge is a consequence of the dispatcher rewrite rather than of anything
 * about these requests. Rev 20 dispatches with Keil's ?C?CCASE runtime, which
 * needs a distinct code address per case entry; Rev 22 dispatches with an
 * inline `MOV DPTR,#0x011E / ADD A,R0 / ADD A,R0 / JMP @A+DPTR` over a table of
 * LJMPs, and three slots can name the same address for free. */
void std_stall_unsupported(void) __naked {
    __asm
        sjmp  .+0x54               ; -> 0x02EF ep0_stall_both
                                   ;    encodes 80 52; PC after the instruction
                                   ;    is 0x029D, 0x029D + 0x52 = 0x02EF
    __endasm;
}
