// MATCH: image=rev22 addr=0x0CC7 len=67 func=oep0_int_handler cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"

/* VECINT 0x00 (OEP0_INT) -- the EP0 OUT data stage completed. Rev 22 at
 * 0x0CC7. Table entry 0x00 is the first word of the table at 0x0C7D and reads
 * 0C C7. (rev20: handler 0x0D25, table 0x0C93.)
 *
 * The host has just delivered the OUT data of a vendor/class control transfer
 * into the EP0 OUT buffer at XDATA 0xFA10. This decodes the single payload
 * byte and turns it into an entry in the deferred-event byte (IRAM BYTE 0x0A),
 * which the main loop dispatches outside interrupt context. Nothing here
 * touches audio hardware; the ISR only posts.
 *
 * 8051 trap: `MOV A,0x0D` at 0x0CCA and 0x0CE7 reads IRAM BYTE 0x0D, the
 * request tag the SETUP handler stashed when it armed the OUT stage. Bit 0x0D
 * (IRAM 0x21.5), which ep0_clamp_len_to_wlength clears at 0x0DC2, is a
 * different object.
 *
 * TAG 1 -> a one-byte mode/route selector. 0x44, 0x80 and 0x00 are the only
 * accepted values and each queues its own event (7, 8, 6). The three tests are
 * independent `if`s, not a chain: stock re-reads the cached byte from R7 before
 * each one (0x0CDA, 0x0CE1) rather than branching away, so a value of 0x44
 * falls through the 0x80 and 0x00 tests too. Only 0x00 could ever match more
 * than one arm, and it cannot, since 0x00 != 0x44 and 0x00 != 0x80.
 * TAG 2 -> a boolean: 1 queues event 4, anything else event 5.
 *
 * Any other tag (including 0) queues nothing and the transfer is still
 * acknowledged, so an unrecognised class request is silently accepted.
 *
 * Termination: clear both phase flags, force the data toggle by setting TOGGLE
 * (bit 5) in IEPCNF0 so the IN status stage goes out as DATA1, then zero both
 * EP0 data counts so the UBM sends the zero-length status packet.
 *
 * ================= REV 20 -> REV 22 DELTA =================
 * NO BEHAVIOURAL CHANGE. Same tags, same three magic bytes, same event
 * numbers, same termination. Rev 20 is 70 bytes at 0x0D25, Rev 22 is 67 at
 * 0x0CC7, and I diffed them byte for byte: the first 63 bytes differ in
 * exactly three positions, all of them addresses.
 *
 *   offset 0x02  JNB displacement  rev20 0x3F -> rev22 0x40
 *                Both mean "branch to the not-armed recovery path". In Rev 20
 *                that path is the four bytes at 0x0D67, inside this function.
 *                In Rev 22 it is 0x0D0A, a separate 7-byte function
 *                (oep0_clear_stall_and_rearm) sitting immediately after this
 *                one -- see that candidate for why it grew from four bytes to
 *                seven.
 *   offset 0x08  LCALL operand     rev20 0x0B11 -> rev22 0x0B1F
 *   offset 0x25  LCALL operand     rev20 0x0B11 -> rev22 0x0B1F
 *                The EP0-OUT-buffer pointer helper. Rev 20's 0x0B11 sets
 *                IRAM 0x1B:0x1C and falls into a tail that loads DPTR from
 *                them; Rev 22's 0x0B1F sets IRAM 0x1D:0x1E and falls into
 *                ep0_load_dptr at 0x0B25. Same constant, 0xFA10, same
 *                DPTR-live-on-return contract.
 *
 *   tail         rev20 `02 0B 82` LJMP ep0_arm_zlp_and_out
 *                rev22 `12 0B 75` LCALL ep0_flush_arm + `22` RET     (+1 byte)
 *   not-armed    rev20 `12 0B 1E` + `22` inline here                 (-4 bytes)
 *                rev22 branches out of the function entirely
 *
 * 63 + 3 + 4 = 70 for Rev 20; 63 + 3 + 1 = 67 for Rev 22.
 *
 * THE IRAM POINTER MOVE IS THE INTERESTING PART, and it is what made the SOF
 * fix possible. In Rev 20 the EP0 buffer pointer lives in IRAM 0x1B (high) and
 * 0x1C (low). In Rev 22 it lives in 0x1D:0x1E, and 0x1B:0x1C are free -- which
 * is precisely where sof_int_handler (rev22 0x0D58) keeps its saved copy of
 * DMABCNT0. So the new watchdog's two bytes of state came out of a relocation
 * of an existing variable pair, not out of new RAM.
 *
 * I checked this by scanning the whole Rev 22 listing for operands 0x1B and
 * 0x1C. Every hit outside sof_int_handler is a BIT operation -- `CLR 0x1B` at
 * 0x0718, `SETB 0x1B` at 0x07D1 and 0x09DA, `SETB 0x1C` at 0x09E5 -- which
 * address IRAM 0x23.3 and 0x23.4, not the bytes. The only BYTE accesses to
 * 0x1B and 0x1C in the image are the four inside sof_int_handler (0x0D6A,
 * 0x0D6F, 0x0D73, 0x0D75). Symmetrically, the only writers of 0x1D/0x1E are
 * ep0_out_buf_ptr_load (0x0B1F) and ep0_in_buf_ptr_load (0x0B37), and the only
 * reader is ep0_load_dptr (0x0B25).
 *
 * ================= HOW IT IS WRITTEN =================
 * Ported from cand/ep0_out_data_handler.c with the two LCALL targets renamed
 * and the not-armed tail turned from an inline call into a branch out of the
 * function. The two decode blocks stay as inline assembly for the reasons the
 * Rev 20 candidate gives: the helper returns with DPTR live (not expressible
 * in C), and Keil caches the payload byte in R7 across all three comparisons
 * while comparing through A, which SDCC will not reproduce. */

extern void ep0_out_buf_ptr_load(void);          /* rev22 0x0B1F, rev20 0x0B11 */
extern void ep0_flush_arm(void);                 /* rev22 0x0B75, rev20 0x0B82 */

void oep0_int_handler(void) {
    __asm
        .globl _ep0_out_buf_ptr_load
        .globl _ep0_flush_arm
    __endasm;

    /* Nothing was expecting an OUT data stage: recover the endpoint instead.
     * Rev 22 branches clean OUT OF THE FUNCTION to do it -- the target 0x0D0A
     * is the byte immediately after this function's RET, i.e. the first
     * instruction of oep0_clear_stall_and_rearm, which was placed adjacent
     * precisely so a 3-byte JNB could reach it.
     *
     * This cannot be written as C. A `goto` to a label in another function is
     * not C, and spelling it as an early `return` after a call costs a 3-byte
     * LCALL plus a RET where stock spends nothing at all beyond the JNB it
     * already needs. It cannot be written as `jnb 0x0b,_oep0_clear_stall_and_rearm`
     * either: a relative branch to an external symbol is not something the
     * linker will resolve. So the displacement is written PC-relative with the
     * assembler's `.`, which is exactly what the encoding is -- `.` is the
     * address of the JNB, +3 skips it, +0x40 is the stock displacement byte.
     * That is correct both when match51 compiles this standalone at 0x0000 and
     * when link51 places it at 0x0CC7. */
    __asm
        jnb   0x0b,(.+0x43)     ; bit 0x0B clear -> 0x0D0A, oep0_clear_stall_and_rearm
    __endasm;

    if (g_class_tag == 1) {
        __asm
            lcall _ep0_out_buf_ptr_load  ; returns with DPTR = 0xFA10
            movx  a,@dptr
            mov   r7,a                   ; cache the payload byte
            cjne  a,#0x44,00201$
            mov   _g_event,#0x07
        00201$:
            mov   a,r7
            cjne  a,#0x80,00202$
            mov   _g_event,#0x08
        00202$:
            mov   a,r7
            jnz   00203$
            mov   _g_event,#0x06
        00203$:
        __endasm;
    }
    if (g_class_tag == 2) {
        __asm
            lcall _ep0_out_buf_ptr_load
            movx  a,@dptr
            cjne  a,#0x01,00204$
            mov   _g_event,#0x04
            sjmp  00205$
        00204$:
            mov   _g_event,#0x05
        00205$:
        __endasm;
    }

    f_stage_out = 0;
    f_stage_in  = 0;
    IEPCNF0 |= 0x20;        /* TOGGLE: send the IN status stage as DATA1 */
    /* Stock uses LCALL + RET at 0x0D06, not a tail LJMP -- the opposite of
     * Rev 20, which LJMPed at 0x0D64. Written out to stop SDCC folding it. */
    __asm
        lcall _ep0_flush_arm
    __endasm;
}
