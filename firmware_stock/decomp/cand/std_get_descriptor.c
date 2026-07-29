// MATCH: image=rev20 addr=0x0173 len=126 func=std_get_descriptor cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void ep0_clamp_len_to_wlength(void);
extern void ep0_in_start_transfer(void);
extern void ep0_stall_both(void);
/* CODE source pointer, 0x19 high and 0x1A low. Transfer length at IRAM
 * 0x09 (low) and 0x0B (high) -- these are BYTE addresses; the bit addresses
 * 0x09 and 0x0B are unrelated flags living in IRAM 0x21. */
__data __at (0x19) unsigned char g_src_hi;
__data __at (0x1A) unsigned char g_src_lo;
__data __at (0x09) unsigned char g_xfer_len_lo;
__data __at (0x0B) unsigned char g_xfer_len_hi;

/* GET_DESCRIPTOR. Only three types are served, and only index 0 for
 * CONFIGURATION.
 *
 * The CONFIGURATION pointer is 0x0670 -- descriptor-block offset +0x0DA,
 * which is the 54-byte VENDOR-CLASS configuration, not the USB Audio Class
 * configuration sitting unreferenced at +0x012. There is no code path that
 * ever serves the audio descriptors; see the reference document.
 *
 * Keil compiled the type-1 test as CJNE but types 2 and 3 as XRL + JNZ. The
 * C mirrors that: `== 1` yields CJNE, `(x ^ n) == 0` yields XRL. */
void std_get_descriptor(void) {
    if (SETUP_wValueH == 1) {                     /* DEVICE */
        g_src_hi = 0x05; g_src_lo = 0x96;         /* 0x0596 */
        /* The helper returns bLength in A; SDCC returns char in DPL, so the
         * capture is assembly. Length high byte is always zero here. */
        __asm
            .globl _code_read_byte_at_srcptr
            lcall _code_read_byte_at_srcptr
            mov   0x09,a
            clr   a
            mov   0x0b,a
        __endasm;
    } else if (((SETUP_wValueH ^ 2) == 0) && (SETUP_wValueL == 0)) {
        g_src_hi = 0x06; g_src_lo = 0x70;         /* 0x0670, vendor config */
        __asm
            mov  dpl,0x1a
            mov  dph,0x19
            mov  a,#0x02
            movc a,@a+dptr          ; wTotalLength low
            mov  0x09,a
            mov  a,#0x03
            movc a,@a+dptr          ; wTotalLength high
            mov  0x0b,a
        __endasm;
    } else if ((SETUP_wValueH ^ 3) == 0) {        /* STRING */
        if (SETUP_wValueL == 0) { g_src_hi = 0x06; g_src_lo = 0xA6; }
        if (SETUP_wValueL == 1) { g_src_hi = 0x06; g_src_lo = 0xAA; }
        if (SETUP_wValueL == 2) { g_src_hi = 0x06; g_src_lo = 0xC8; }
        __asm
            lcall _code_read_byte_at_srcptr
            mov   0x09,a
            clr   a
            mov   0x0b,a
        __endasm;
    } else {
        ep0_stall_both();
        return;
    }
    ep0_clamp_len_to_wlength();
    ep0_in_start_transfer();
}
