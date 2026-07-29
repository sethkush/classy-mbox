// MATCH: image=rev22 addr=0x0B25 len=7 func=ep0_load_dptr cflags=--peep-file,firmware_stock/decomp/keil.peep entry=1
/* Load DPTR from the EP0 working pointer held in IRAM 0x1D:0x1E.
 *
 * Byte order: 0x1D is the HIGH byte and 0x1E the low -- Keil's convention for
 * xdata pointers, the opposite of SDCC's, which is why the pair cannot be
 * declared as an SDCC pointer variable and every access goes through this
 * helper. Setting DPTR as a side effect is not expressible in C, so this is
 * naked assembly with the IRAM addresses written numerically (a symbolic
 * reference to an otherwise-unused __at variable would never be emitted, and
 * the assembler would fail on the undefined symbol).
 *
 * Factored out because LCALL costs 3 bytes against 6 inline, over eight call
 * sites: 0x0074, 0x007D, 0x0099, 0x00B8, 0x00DD, 0x0162, 0x0232, 0x0ACD.
 *
 * ENTRY POINT, not a function: these seven bytes are the tail of
 * ep0_out_buf_ptr_load (0x0B1F), which falls through into them. They are
 * claimed by that candidate; this one exists so callers can name the address,
 * and is linked as an absolute equate rather than placed.
 *
 * REV 20 -> REV 22 DELTA: same seven bytes and the same merge as rev20
 * dptr_from_ep0_ptr at 0x0B17, with the IRAM pair renumbered 0x1B:0x1C ->
 * 0x1D:0x1E. Behaviour unchanged. */
void ep0_load_dptr(void) __naked {
    __asm
        mov  dpl,0x1e      ; low  byte of the EP0 working pointer
        mov  dph,0x1d      ; high byte
        ret
    __endasm;
}
