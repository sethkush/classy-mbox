// MATCH: image=rev20 addr=0x02EA len=4 func=std_request_unknown_default cflags=--peep-file,firmware_stock/decomp/keil.peep
/* The `default:` arm of the standard-request switch in std_request_dispatch
 * (0x0118): any bRequest with no entry in the ?C?CCASE table -- that is 2 and
 * 4 (reserved in USB 2.0 table 9-4) and everything above 12 -- stalls EP0 in
 * both directions and returns.
 *
 * Rev 20 therefore does NOT delegate unknown standard requests anywhere; it
 * answers them with a protocol STALL. (The only reference to 0x02EA in the
 * image is the two default-target bytes 02 ea at 0x0142..0x0143 -- the tail of
 * the ?C?CCASE table, whose last four bytes at 0x0140..0x0143 are
 * 00 00 02 ea: the 00 00 sentinel followed by the default address, high byte
 * first. A byte scan finds
 * no LJMP or LCALL to it.)
 *
 * WRITTEN AS ASSEMBLY, for a four-byte reason worth spelling out. The three
 * sibling arms at 0x0299, 0x029C and 0x02E7 do the same thing in three bytes
 * (`LJMP ep0_stall_both`); this one spends four (`LCALL ep0_stall_both` +
 * `RET`). Plain C gives the three-byte form: SDCC's tail-call peephole rewrites
 * LCALL/RET into LJMP, and there is no way to ask it not to, so C cannot
 * express the stock encoding here.
 *
 * The reading that fits the layout -- and it is an inference, not something the
 * bytes prove -- is that Keil folds "call, then jump to the function's RET"
 * into a bare LJMP, and this arm is the one physically adjacent to the RET, so
 * there was no jump to fold: it simply calls and falls into the epilogue. Rev
 * 22 supports it. There the same construct is at 0x02EF and is again
 * `LCALL 0x1001 / RET`, four bytes, while its merged stall arm at rev22 0x029B
 * reaches it with a two-byte `SJMP 0x02EF` -- i.e. the default arm is again the
 * one holding the function's return.
 */
void std_request_unknown_default(void) __naked {
    __asm
        .globl _ep0_stall_both
        lcall _ep0_stall_both      ; 0x1009: stall EP0 IN and EP0 OUT
        ret
    __endasm;
}
