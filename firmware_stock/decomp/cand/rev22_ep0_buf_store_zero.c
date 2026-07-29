// MATCH: image=rev22 addr=0x0B5B len=8 func=ep0_buf_store_zero cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Zero one byte of the EP0 buffer. The caller passes the LOW address byte in
 * A -- Keil's register convention -- and the high byte comes from the EP0
 * working pointer at IRAM 0x1D. Callers reach here after incrementing IRAM
 * 0x1E, so A already holds the updated low byte.
 *
 * Naked: the parameter arrives in A, which SDCC's calling convention would
 * never do, and the IRAM address is written numerically because inline asm is
 * opaque to the compiler.
 *
 * REV 20 -> REV 22 DELTA: same 8 bytes and same shape as rev20
 * ep0_buf_clear_byte at 0x0B36, with one operand changed: the pointer high
 * byte moved from IRAM 0x1B to IRAM 0x1D. That is the same EP0-pointer
 * renumbering seen in ep0_load_dptr, ep0_in_buf_ptr_load,
 * ep0_out_buf_ptr_load and ep0_in_send_chunk. Behaviour unchanged. */
void ep0_buf_store_zero(void) __naked {
    __asm
        mov  dpl,a         ; low address byte, passed in A
        mov  dph,0x1d      ; high byte from the EP0 working pointer
        clr  a
        movx @dptr,a
        ret
    __endasm;
}
