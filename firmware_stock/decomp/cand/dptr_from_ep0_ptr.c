// MATCH: image=rev20 addr=0x0B17 len=7 func=dptr_from_ep0_ptr cflags=--peep-file,firmware_stock/decomp/keil.peep entry=1
/* Load DPTR from the EP0 working pointer held in IRAM 0x1B:0x1C.
 *
 * Byte order: 0x1B is the HIGH byte and 0x1C the low. That is Keil's
 * convention for xdata pointers and the opposite of SDCC's, which is why the
 * pointer cannot be declared as an SDCC pointer variable at 0x1B and why
 * every access goes through this helper.
 *
 * The original factored it out because LCALL costs 3 bytes against 6 for the
 * inlined load, across twelve call sites. Setting DPTR as a side effect is
 * not expressible in C, so this is naked assembly. IRAM addresses are written
 * numerically because inline asm is opaque to the compiler, so a symbolic
 * reference to an unused __at variable would not be emitted. */
void dptr_from_ep0_ptr(void) __naked {
    __asm
        mov  dpl,0x1c      ; low  byte of the EP0 pointer
        mov  dph,0x1b      ; high byte
        ret
    __endasm;
}
