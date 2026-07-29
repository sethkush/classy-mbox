// MATCH: image=rev22 addr=0x0ABB len=100 func=ep0_in_send_chunk cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"

/* Stage up to one 8-byte packet of an EP0 IN data stage: copy bytes from the
 * CODE source pointer into the EP0 IN buffer at XDATA 0xFA18, publish the byte
 * count in IEPDCNTX0, and decide whether this was the last packet.
 *
 * Called for the first packet by ep0_in_stage_and_go (0x0B63) and for every
 * following packet from the IEP0 interrupt path.
 *
 * State, all IRAM bytes (NOT bit addresses -- 0x09 and 0x0B as bits are
 * unrelated flags living in IRAM 0x21):
 *   0x09      transfer length, LOW byte
 *   0x0B      transfer length, HIGH byte
 *   0x18      bytes placed in this packet, 0..8
 *   0x19:0x1A CODE source pointer, 0x19 high / 0x1A low
 *   0x1D:0x1E EP0 buffer pointer,  0x1D high / 0x1E low
 * and the bits
 *   0x0B (IRAM 0x21.3) f_stage_out, 0x0C (0x21.4) f_stage_in,
 *   0x0D (0x21.5) short-packet-owed, set by ep0_clamp_len_to_wlength.
 *
 * The 0x09-low / 0x0B-high assignment is proved inside this function by the
 * borrow at 0x0AE1: DJNZ on 0x09, and only when that underflows is 0x09
 * reloaded with 0xFF and 0x0B decremented.
 *
 * ---- REV 20 -> REV 22 DELTA: same algorithm, plumbing only ----------------
 * Rev 20 has this at 0x0B8C, 98 bytes, as ep0_in_fill_chunk. Diffing the two
 * byte strings, every difference is one of four mechanical things:
 *
 *   1. The EP0 buffer pointer was renumbered from IRAM 0x1B:0x1C (Rev 20) to
 *      0x1D:0x1E (Rev 22). Visible here at 0x0AD1/0x0AD3/0x0AD7 against Rev 20
 *      0x0BA0/0x0BA2/0x0BA6, and confirmed independently by
 *      ep0_out_buf_ptr_load (0x0B1F) and ep0_load_dptr (0x0B25), which write
 *      and read that pair. IRAM 0x19:0x1A, the CODE source pointer, did NOT
 *      move.
 *   2. The "point at the IN buffer" helper moved, 0x0B3E -> 0x0B37.
 *   3. The source-byte read was split in two. Rev 20 called one helper,
 *      code_read_byte_at_srcptr (0x0B6E) = load DPTR from 0x19:0x1A, CLR A,
 *      MOVC, RET, and then dptr_from_ep0_ptr (0x0B17) to swing DPTR to the
 *      destination while A survived. Rev 22 splits the first helper: 0x0B6E is
 *      now load_dptr_from_ptr19, DPTR load and RET only, and the `CLR A / MOVC
 *      A,@A+DPTR` is inlined here at 0x0ACB. Those two inlined bytes are the
 *      entire size difference, 98 -> 100.
 *   4. Two relative-branch displacements shifted to follow (0x0AC6, 0x0AF4).
 *
 * Nothing else differs: same 8-byte packet size, same NAK-then-OR publish,
 * same end-of-stage decision, and the same two quirks noted below. This is a
 * recompile, not a fix. */

__data __at (0x09) unsigned char g_xfer_len_lo;
__data __at (0x0B) unsigned char g_xfer_len_hi;
__data __at (0x18) unsigned char g_chunk_len;
__bit  __at (0x0D) f_short_wanted;

void ep0_in_send_chunk(void) {
    /* The copy loop is assembly because three separate things in it are not
     * expressible in C:
     *   - ep0_in_buf_ptr_load is entered with A already zero, left over from
     *     `g_chunk_len = 0`;
     *   - the byte read spans two calls with A live across the second
     *     (load_dptr_from_ptr19 sets DPTR, MOVC leaves the byte in A, then
     *     ep0_load_dptr swings DPTR to the destination without touching A);
     *   - the 16-bit decrement is a DJNZ with a hand-rolled borrow.
     *
     * Loop shape: length test at the top, counter test at the bottom, i.e.
     * `do { if (len_lo == 0) break; ...; } while (++n != 8)`.
     *
     * Two behaviours worth recording, both unchanged from Rev 20:
     *
     * (a) The top test at 0x0AC1 looks at the LOW length byte only and treats
     *     zero as "nothing left". The borrow below can never produce
     *     (hi != 0, lo == 0) -- it reloads lo with 0xFF -- but
     *     ep0_clamp_len_to_wlength can, because it copies both bytes straight
     *     out of wLength. A transfer whose length is an exact multiple of 256
     *     therefore stages an empty packet here. This is a code-reading claim
     *     about 0x0AC1, not a hardware observation.
     *
     * (b) `SETB C / SUBB A,#0` is Keil's ">= 1", i.e. "!= 0" for an unsigned
     *     byte; that is why the source says `>= 1` rather than `!= 0`. */
    __asm
        .globl _ep0_in_buf_ptr_load
        .globl _load_dptr_from_ptr19
        .globl _ep0_load_dptr

        clr   a
        mov   0x18,a                  ; g_chunk_len = 0
        lcall _ep0_in_buf_ptr_load    ; 0x1D:0x1E = 0xFA18; entered with A = 0
    00301$:
        mov   a,0x09                  ; transfer length, low byte
        setb  c
        subb  a,#0x00
        jc    00304$                  ; low byte zero -> this packet is done
        lcall _load_dptr_from_ptr19   ; DPTR = CODE source pointer 0x19:0x1A
        clr   a
        movc  a,@a+dptr               ; A = the source byte  (inlined in rev22)
        lcall _ep0_load_dptr          ; DPTR = EP0 buffer pointer, A preserved
        movx  @dptr,a
        inc   0x1e                    ; ++EP0 buffer pointer (low, then carry)
        mov   a,0x1e
        jnz   00302$
        inc   0x1d
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
        cjne  a,#0x08,00301$          ; 8 = EP0 max packet size
    00304$:
    __endasm;

    /* Publish the packet. Writing 0x80 sets NAK (bit 7) and zeroes the count
     * in one store; the OR then merges the byte count in with NAK still set,
     * so the packet is staged but not yet offered to the host. The caller
     * clears NAK: ep0_in_stage_and_go (0x0B63) for the first packet, the IEP0
     * interrupt path for the rest. */
    IEPDCNTX0 = 0x80;
    IEPDCNTX0 |= g_chunk_len;

    /* Default: more of this transfer is still to come, so the next EP0 event
     * we expect is another IN, not the status stage. */
    f_stage_out = 1;
    f_stage_in  = 0;

    if (g_xfer_len_lo == 0 && g_xfer_len_hi == 0) {
        /* All data staged. One further IN is still owed if this packet was
         * full AND the host asked for more than we had, because the host is
         * then still waiting for a short packet to terminate the data stage.
         * Otherwise this packet was itself short and the stage is over.
         * The first arm re-asserts the two flags the code above already set --
         * redundant in both images, and reproduced because it is in the stock
         * bytes at 0x0B15. */
        if (g_chunk_len == 8 && f_short_wanted) {
            f_stage_out = 1;
            f_stage_in  = 0;
        } else {
            f_stage_out = 0;
            f_stage_in  = 1;
        }
    }
}
