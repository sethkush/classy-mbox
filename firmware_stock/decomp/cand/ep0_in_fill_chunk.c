// MATCH: image=rev20 addr=0x0B8C len=98 func=ep0_in_fill_chunk cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"

/* Copy up to one 8-byte packet from the CODE source pointer into the EP0 IN
 * buffer, set the packet byte count, and decide whether this was the last
 * packet of the data stage. Called by ep0_in_start_transfer (0x0B77) for the
 * first packet and again from the IEP0 interrupt path at 0x0FC7 for each
 * following one.
 *
 * State it works on, all in IRAM:
 *   0x09      transfer length, LOW byte
 *   0x0B      transfer length, HIGH byte   (BYTE 0x0B; unrelated to BIT 0x0B)
 *   0x18      bytes placed in this packet, 0..8
 *   0x19:0x1A CODE source pointer, 0x19 high / 0x1A low
 *   0x1B:0x1C EP0 buffer pointer,  0x1B high / 0x1C low
 *   bit 0x0B  f_stage_out, bit 0x0C f_stage_in, bit 0x0D short-packet wanted
 *
 * The 0x09/0x0B roles are proved by the decrement at 0x0BB0: DJNZ on 0x09,
 * and only when that reaches zero does it reload 0x09 with 0xFF and DEC 0x0B.
 * That is a borrow, so 0x09 is the low byte.
 *
 * Rev 22 has this at 0x0ABB, 100 bytes. Same algorithm, same constants, same
 * flag handling; the differences are all plumbing. The EP0 buffer pointer
 * moved from IRAM 0x1B:0x1C to 0x1D:0x1E, the two helpers moved (0x0B3E ->
 * 0x0B37, 0x0B17 -> 0x0B25), and the source-byte read was split: Rev 22's
 * 0x0B6E loads DPTR from 0x19:0x1A and returns, with the `CLR A / MOVC
 * A,@A+DPTR` inlined at the call site, which is the two extra bytes. The
 * length bytes stay at IRAM 0x09 and 0x0B. */

__data __at (0x09) unsigned char g_xfer_len_lo;
__data __at (0x0B) unsigned char g_xfer_len_hi;
__data __at (0x18) unsigned char g_chunk_len;
__bit  __at (0x0D) f_short_wanted;   /* IRAM 0x21.5 -- see ep0_clamp_len_to_wlength */

void ep0_in_fill_chunk(void) {
    /* The copy loop is assembly for three reasons, all of them Keil holding a
     * value in a register across a call:
     *   - code_read_byte_at_srcptr (0x0B6E) returns the byte in A, and
     *     dptr_from_ep0_ptr (0x0B17) is then called *without disturbing it* so
     *     that the MOVX @DPTR,A at 0x0B9F stores it. Two callees cooperating
     *     through A and DPTR is not expressible in C.
     *   - ep0_ptr_set_in_buf is entered with A already zero from the
     *     `g_chunk_len = 0` above it.
     *   - the 16-bit decrement is a DJNZ with a hand-rolled borrow.
     *
     * Loop shape: the length test is at the top and the counter test at the
     * bottom, i.e. `do { if (len == 0) break; ...; } while (++n != 8)`.
     *
     * The top test at 0x0B92 looks at the LOW byte only, and treats zero as
     * "nothing left". Combined with the borrow below -- which reloads the low
     * byte with 0xFF, never 0x00 -- the pair (hi != 0, lo == 0) is a state the
     * decrement can never produce, but ep0_clamp_len_to_wlength can, because
     * it loads both bytes straight from wLength. A control transfer whose
     * length is an exact multiple of 256 therefore sends a zero-byte packet
     * here instead of its data. Reachable in principle via
     * GET_DESCRIPTOR(CONFIGURATION) with wLength == 0x0100; not observed on
     * hardware, stated here as a code-reading claim about 0x0B92 only. */
    __asm
        .globl _ep0_ptr_set_in_buf
        .globl _code_read_byte_at_srcptr
        .globl _dptr_from_ep0_ptr

        clr   a
        mov   0x18,a                  ; g_chunk_len = 0
        lcall _ep0_ptr_set_in_buf     ; 0x1B:0x1C = 0xFA18, enters with A = 0
    00301$:
        mov   a,0x09                  ; length low
        setb  c
        subb  a,#0x00                 ; Keil's ">= 1"
        jc    00304$                  ; low byte zero -> packet is complete
        lcall _code_read_byte_at_srcptr   ; A = CODE[0x19:0x1A]
        lcall _dptr_from_ep0_ptr          ; DPTR = 0x1B:0x1C, A preserved
        movx  @dptr,a
        inc   0x1c                    ; ++EP0 buffer pointer
        mov   a,0x1c
        jnz   00302$
        inc   0x1b
    00302$:
        inc   0x1a                    ; ++CODE source pointer
        mov   a,0x1a
        jnz   00303$
        inc   0x19
    00303$:
        djnz  0x09,00305$             ; 16-bit --length
        mov   a,0x0b
        setb  c
        subb  a,#0x00
        jc    00305$                  ; high byte zero: no borrow available
        mov   0x09,#0xff
        dec   0x0b
    00305$:
        inc   0x18
        mov   a,0x18
        cjne  a,#0x08,00301$          ; 8 bytes is the EP0 max packet size
    00304$:
    __endasm;

    /* Publish the packet. Writing 0x80 first sets NAK and zeroes the count in
     * one go; the OR then merges the byte count in with NAK still set, so the
     * packet is staged but not yet offered. ep0_in_start_transfer clears NAK
     * afterwards for the first packet; for later packets the interrupt path
     * at 0x0FC7 does it. */
    IEPDCNTX0 = 0x80;
    IEPDCNTX0 |= g_chunk_len;

    /* Default: more of this transfer is still to come. */
    f_stage_out = 1;
    f_stage_in  = 0;

    if (g_xfer_len_lo == 0 && g_xfer_len_hi == 0) {
        /* All the data has been staged. One more IN is still owed if this
         * packet was full *and* the host asked for more than we had, because
         * then the host is still expecting a short packet to end the stage.
         * Otherwise this packet was itself short and the data stage is over,
         * so the next event is the status stage. Stock re-asserts the same two
         * flags in the first arm that it already set above -- redundant, and
         * reproduced because it is in the stock bytes at 0x0BE4. */
        if (g_chunk_len == 8 && f_short_wanted) {
            f_stage_out = 1;
            f_stage_in  = 0;
        } else {
            f_stage_out = 0;
            f_stage_in  = 1;
        }
    }
}
