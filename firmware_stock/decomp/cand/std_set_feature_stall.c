// MATCH: image=rev20 addr=0x029C len=3 func=std_set_feature_stall cflags=--peep-file,firmware_stock/decomp/keil.peep
extern void ep0_stall_both(void);

/* The SET_FEATURE arm of the standard-request switch in std_request_dispatch
 * (0x0118). The ?C?CCASE table entry for bRequest = 3 points here; see
 * cand/std_request_dispatch.c for the table.
 *
 * This is not a function in the source, it is one case arm: the only reference
 * to 0x029C in the whole image is the table entry, and the arm's body is a
 * single call that ends the switch, so Keil folded "call, then jump to the
 * function's RET" into a bare LJMP. SDCC reaches the same three bytes because
 * its own tail-call peephole rewrites LCALL/RET to LJMP.
 *
 * Behaviour: SET_FEATURE is refused. EP0 IN and OUT are both stalled
 * (ep0_stall_both, 0x1009), so the host sees a protocol STALL. On the wire this
 * is indistinguishable from an unrecognised bRequest, which reaches the same
 * routine through std_request_unknown_default (0x02EA) -- having a table entry
 * buys nothing observable, it is just how the switch was written. The only
 * difference is the encoding: four bytes there, three here, and that file
 * explains why.
 *
 * Rev 22 keeps the behaviour and merges the arms. Its dispatcher is a dense
 * jump table (rev22 0x010B, table at rev22 0x011E) whose entries for bRequest
 * 3, 7 and 12 all point at rev22 0x029B -- one shared two-byte `SJMP 0x02EF`
 * into the default arm, which is `LCALL 0x1001 / RET`. Ghidra's rev22 listing
 * names 0x029B std_stall_unsupported. So the three separate three-byte stubs
 * here became a single two-byte one there.
 */
void std_set_feature_stall(void) { ep0_stall_both(); }
