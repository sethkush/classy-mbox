// MATCH: image=rev20 addr=0x0B11 len=13 func=dptr_to_ep0_out_buf cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Point the EP0 working pointer at the OUT buffer (XDATA 0xFA10) and load
 * DPTR from it. Ghidra splits this at 0x0B17 because dptr_from_ep0_ptr has
 * its own callers; at source level the two are one function and this one
 * simply falls through into that tail. */
void dptr_to_ep0_out_buf(void) __naked {
    __asm
        mov  0x1b,#0xfa
        mov  0x1c,#0x10
        mov  dpl,0x1c
        mov  dph,0x1b
        ret
    __endasm;
}
