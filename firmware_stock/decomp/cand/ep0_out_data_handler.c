// MATCH: image=rev20 addr=0x0D25 len=70 func=ep0_out_data_handler cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void ep0_arm_zlp_and_out(void);

/* OEP0 data-stage completion, reached from the USB interrupt dispatcher at
 * 0x0C93. The host has just delivered the OUT data of a vendor/class control
 * transfer into the EP0 OUT buffer at XDATA 0xFA10; this decodes the single
 * payload byte and turns it into an entry in the deferred-event byte
 * (IRAM 0x0A), which the main loop dispatches outside interrupt context.
 *
 * IRAM 0x0D is used here as a BIT (0x21.5) nowhere -- the `MOV A,0x0D` at
 * 0x0D28 and 0x0D45 reads IRAM BYTE 0x0D, g_class_tag, the request tag the
 * SETUP handler stashed when it armed the OUT stage. Bit 0x0D, which
 * ep0_clamp_len_to_wlength sets, is a different object living in IRAM 0x21.
 *
 * Tag 1 -> a one-byte mode/route selector: 0x44, 0x80 and 0x00 are the only
 * accepted values and each queues its own event. Note the three tests are
 * independent `if`s, not a chain: stock re-reads the cached byte from R7
 * before each one (0x0D38, 0x0D3F) rather than branching away, so a value of
 * 0x44 falls through the 0x80 and 0x00 tests too. Only 0x00 could ever match
 * more than one arm, and it cannot, since 0x00 != 0x44 and 0x00 != 0x80.
 * Tag 2 -> a boolean: 1 queues event 4, anything else event 5.
 *
 * Any other tag (including 0) queues nothing and the transfer is still
 * acknowledged, so an unrecognised class request is silently accepted.
 *
 * Termination: clear both phase flags, force the data toggle by setting
 * TOGGLE (bit 5) in IEPCNF0 so the status stage goes out as DATA1, then arm
 * both endpoints for the zero-length status packet.
 *
 * Rev 22 has this function at 0x0CC7, 68 bytes, and it is byte-identical
 * apart from three things: the OUT-buffer pointer helper moved from 0x0B11 to
 * 0x0B1F (and with it the EP0 pointer from IRAM 0x1B:0x1C to 0x1D:0x1E), the
 * final tail call became `LCALL 0x0B75 / RET` instead of Rev 20's `LJMP
 * 0x0B82`, and the opening JNB displacement therefore reads 0x40 not 0x3F.
 * Same tags, same three magic bytes, same event numbers. */
void ep0_out_data_handler(void) {
    __asm
        .globl _dptr_to_ep0_out_buf
        .globl _ep0_clear_stall_toggle_and_arm
    __endasm;

    /* Nothing was expecting an OUT data stage: recover the endpoint instead. */
    if (!f_stage_out) goto unarmed;

    if (g_class_tag == 1) {
        /* dptr_to_ep0_out_buf points IRAM 0x1B:0x1C at 0xFA10 *and* returns
         * with DPTR loaded from it, so the load is a bare MOVX. Passing DPTR
         * out of a callee is not expressible in C. The byte is then cached in
         * R7 across all three tests, which is Keil comparing through A while
         * keeping the copy -- SDCC either drops the copy or compares R7
         * directly, so this block is written out. */
        __asm
            lcall _dptr_to_ep0_out_buf
            movx  a,@dptr
            mov   r7,a
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
            lcall _dptr_to_ep0_out_buf
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
    IEPCNF0 |= 0x20;        /* TOGGLE: send the status stage as DATA1 */
    ep0_arm_zlp_and_out();  /* tail call: stock LJMPs, 0x0D64 */
    return;

unarmed:
    /* Stock uses LCALL + RET here (0x0D67), not a tail LJMP, so the call is
     * written out to stop SDCC folding it. */
    __asm
        lcall _ep0_clear_stall_toggle_and_arm
    __endasm;
}
