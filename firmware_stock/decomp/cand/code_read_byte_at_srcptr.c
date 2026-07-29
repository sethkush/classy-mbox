// MATCH: image=rev20 addr=0x0B6E len=9 func=code_read_byte_at_srcptr cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Read the first byte of the descriptor at the CODE source pointer held in
 * IRAM 0x19:0x1A (0x19 high, 0x1A low -- Keil's byte order again). For every
 * descriptor that first byte is bLength, which is how the transfer length is
 * obtained. Naked: the result comes back in A, and MOVC cannot be expressed
 * in C against a pointer the compiler does not own. */
void code_read_byte_at_srcptr(void) __naked {
    __asm
        mov  dpl,0x1a
        mov  dph,0x19
        clr  a
        movc a,@a+dptr
        ret
    __endasm;
}
