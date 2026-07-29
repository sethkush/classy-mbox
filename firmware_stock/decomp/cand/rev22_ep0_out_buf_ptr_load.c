// MATCH: image=rev22 addr=0x0B1F len=13 func=ep0_out_buf_ptr_load cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Point the EP0 working pointer at the OUT buffer (XDATA 0xFA10) and load DPTR
 * from it. Called from the class OUT data-stage handlers at 0x0CCF and 0x0CEC.
 *
 * SPAN. Ghidra splits this at 0x0B25, calling the second half ep0_load_dptr,
 * because that address has eight callers of its own. It is not a separate
 * source function -- this one simply falls through into it, which is Keil tail
 * merging. The whole 13-byte run 0x0B1F..0x0B2B is claimed here; 0x0B25 is
 * proposed for symbols.map as an entry point (proposed/ep0.symbols) and
 * carried in its own entry=1 candidate.
 *
 * REV 20 -> REV 22 DELTA: structurally identical to rev20 dptr_to_ep0_out_buf
 * at 0x0B11 (13 bytes, same merge, same 0xFA10 constant). The only change is
 * the IRAM pair: 0x1B:0x1C -> 0x1D:0x1E. This function and ep0_load_dptr are
 * the direct proof of that renumbering -- the same routine writes the pair and
 * then reads it back into DPTR, so hi/lo and the addresses are both pinned by
 * these thirteen bytes alone. */
void ep0_out_buf_ptr_load(void) __naked {
    __asm
        mov  0x1d,#0xfa    ; EP0 working pointer, HIGH byte
        mov  0x1e,#0x10    ; ... low byte -> XDATA 0xFA10, the EP0 OUT buffer
        ;; ---- entry point ep0_load_dptr, stock 0x0B25 ----
        mov  dpl,0x1e
        mov  dph,0x1d
        ret
    __endasm;
}
