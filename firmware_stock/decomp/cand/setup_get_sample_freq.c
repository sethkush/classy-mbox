// MATCH: image=rev20 addr=0x008A len=142 func=setup_get_sample_freq cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* EP0 working pointer, Rev 20 addresses -- see the note in mbox.h. */
__data __at (0x1B) unsigned char g_ep0_ptr_hi;
__data __at (0x1C) unsigned char g_ep0_ptr_lo;
extern void ep0_ptr_set_in_buf(void);
extern void ep0_buf_clear_byte(void);   /* takes the low address byte in A */
extern void ep0_stall_both(void);
__data __at (0x08) unsigned char g_clock_mode;

/* Audio class GET_CUR, SAMPLING_FREQ_CONTROL on the streaming endpoint
 * (bmRequestType 0xA2). Returns the current rate as 3 bytes, little endian.
 *
 * The reply is built by walking the EP0 IN buffer with the pointer in IRAM
 * 0x1B:0x1C. `++pl; if (pl == 0) ++ph;` is the 16-bit advance, and it leaves
 * the new low byte in A, which is exactly the parameter ep0_buf_clear_byte
 * expects. Stores of a non-zero byte load DPTR through the helper and so are
 * written as assembly -- see dptr_from_ep0_ptr.
 *
 * This function is also the authority on the clock-mode numbering: it reads
 * the same IRAM 0x08 that audio_clock_mode_apply writes, so mode 2 = 44100
 * and mode 3 = 48000 are established by the firmware itself. */
void setup_get_sample_freq(void) {
    if ((SETUP_wValueH ^ 1) != 0) { ep0_stall_both(); return; }
    ep0_ptr_set_in_buf();

    if (g_clock_mode == 1) {                 /* idle: report 0 Hz */
        __asm
            .globl _dptr_from_ep0_ptr
            lcall _dptr_from_ep0_ptr
            clr   a
            movx  @dptr,a
        __endasm;
        ++g_ep0_ptr_lo; if (g_ep0_ptr_lo == 0) ++g_ep0_ptr_hi;
        ep0_buf_clear_byte();
        ++g_ep0_ptr_lo; if (g_ep0_ptr_lo == 0) ++g_ep0_ptr_hi;
        ep0_buf_clear_byte();
    } else if (g_clock_mode == 2) {          /* 0x00AC44 = 44100 */
        __asm
            lcall _dptr_from_ep0_ptr
            mov   a,#0x44
            movx  @dptr,a
        __endasm;
        ++g_ep0_ptr_lo; if (g_ep0_ptr_lo == 0) ++g_ep0_ptr_hi;
        __asm
            mov   dpl,a
            mov   dph,0x1b
            mov   a,#0xac
            movx  @dptr,a
        __endasm;
        ++g_ep0_ptr_lo; if (g_ep0_ptr_lo == 0) ++g_ep0_ptr_hi;
        ep0_buf_clear_byte();
    } else if (g_clock_mode == 3) {          /* 0x00BB80 = 48000 */
        __asm
            lcall _dptr_from_ep0_ptr
            mov   a,#0x80
            movx  @dptr,a
        __endasm;
        ++g_ep0_ptr_lo; if (g_ep0_ptr_lo == 0) ++g_ep0_ptr_hi;
        __asm
            mov   dpl,a
            mov   dph,0x1b
            mov   a,#0xbb
            movx  @dptr,a
        __endasm;
        ++g_ep0_ptr_lo; if (g_ep0_ptr_lo == 0) ++g_ep0_ptr_hi;
        ep0_buf_clear_byte();
    } else {
        ep0_stall_both();
        return;
    }

    /* The shared reply tail. Ghidra calls this a separate function at 0x010D,
     * because two other handlers LCALL it -- but at source level it is the end
     * of this function, and Keil merged the two. The evidence is the encoding:
     * all three success paths above reach it with a two-byte SJMP, and a short
     * jump only reaches an adjacent target. Written as a call instead, the same
     * source yields a three-byte LJMP through a trampoline.
     *
     * send_3byte_ep0_reply is therefore declared as an entry point at 0x010D
     * (firmware_stock/decomp/symbols.map) rather than compiled separately, the
     * same treatment dptr_from_ep0_ptr gets inside dptr_to_ep0_out_buf. */
    IEPDCNTX0 = 3;
    f_stage_out = 0;
    f_stage_in = 1;
}
