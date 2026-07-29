// MATCH: image=rev22 addr=0x02EF len=4 func=ep0_stall_both cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Protocol-STALL both halves of EP0: the outermost of the three stall entry
 * points, and the one that owns the RET.
 *
 * The real work is in ep0_stall_both_clear_phase_flags (rev22 0x1001), which
 * sets the STALL bit in IEPCNFG_0 (XDATA 0xFF68) and OEPCNFG_0 (0xFFA8) and
 * clears the two EP0 data-phase flags. This four-byte routine is LCALL + RET
 * over it, and it is what eight sites across the standard-request machinery
 * actually call (Ghidra records XREFs from 0x008E, 0x0100, 0x0114, 0x0159,
 * 0x01E3, 0x022C, 0x0262, 0x029B).
 *
 * WRITTEN AS ASSEMBLY for a four-byte reason. Plain C gives the three-byte form
 * -- SDCC's tail-call peephole rewrites LCALL/RET into LJMP and there is no way
 * to ask it not to -- so C cannot express the stock encoding. The reading that
 * fits the layout, and it is an inference rather than something the bytes
 * prove, is that Keil folds "call, then jump to the function's RET" into a bare
 * LJMP, and this arm is the one physically adjacent to the RET, so there was no
 * jump to fold: it calls and falls into the epilogue. Its siblings do get the
 * folded form -- thunk_stall_ep0 at 0x0100 is a bare LJMP here, and
 * std_stall_unsupported at 0x029B is a bare SJMP.
 *
 * REV 20 -> REV 22 DELTA: same construct, different Ghidra name, both operands
 * relocated. The Rev 20 counterpart is std_request_unknown_default at 0x02EA
 * (LCALL 0x1009 / RET); Rev 22 has it at 0x02EF (LCALL 0x1001 / RET). Ported
 * from cand/std_request_unknown_default.c with only the callee symbol changed.
 *
 * The names diverge because the two images reach it differently, and that IS a
 * behavioural difference worth stating:
 *
 *   * Rev 20 dispatches standard requests through Keil's ?C?CCASE table, whose
 *     trailing default-address field (0x0140..0x0143 = 00 00 02 ea) names
 *     0x02EA. So in Rev 20 this address is specifically the `default:` arm --
 *     bRequest 2, 4, or anything above 12.
 *   * Rev 22 dispatches through an inline JMP @A+DPTR over a table of 3-byte
 *     LJMPs at 0x011E, with an explicit range check (CJNE #0x0D / JC) that
 *     sends bRequest > 12 to 0x0114 -> LJMP 0x02EF. The reserved codes 2 and 4
 *     get their own table slots (0x0124, 0x012A) that also LJMP 0x02EF.
 *
 * Same outcome in both images -- unknown standard requests get a protocol STALL
 * and are NOT delegated anywhere -- but Rev 22 reaches it from the jump table
 * rather than from a ?C?CCASE default field. */
void ep0_stall_both(void) __naked {
    __asm
        .globl _ep0_stall_both_clear_phase_flags
        lcall _ep0_stall_both_clear_phase_flags   ; rev22 0x1001
        ret
    __endasm;
}
