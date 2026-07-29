// MATCH: image=rev20 addr=0x0B36 len=8 func=ep0_buf_clear_byte cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Zero one byte of the EP0 buffer. The caller passes the LOW address byte in
 * A -- Keil's register convention -- and the high byte comes from the EP0
 * pointer at IRAM 0x1B. Callers reach here after incrementing IRAM 0x1C, so A
 * already holds the updated low byte. Naked: the parameter arrives in A,
 * which SDCC's calling convention would not do. */
void ep0_buf_clear_byte(void) __naked {
    __asm
        mov  dpl,a         ; low address byte, passed in A
        mov  dph,0x1b      ; high byte from the EP0 pointer
        clr  a
        movx @dptr,a
        ret
    __endasm;
}
