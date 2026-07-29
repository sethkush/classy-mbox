// MATCH: image=rev20 addr=0x0073 len=23 func=setup_get_input_source cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void ep0_ptr_set_in_buf(void);
extern void ep0_send_1byte(void);
/* Audio class GET_CUR on the Selector Unit (terminal ID 5). Reports which
 * input pin is selected: 1 = analog (input terminal 2), 2 = S/PDIF (input
 * terminal 6). The reply byte is stored through the EP0 working pointer, so
 * the store is assembly -- see dptr_from_ep0_ptr for why. */
void setup_get_input_source(void) {
    ep0_ptr_set_in_buf();
    if (f_spdif) {
        __asm
            .globl _dptr_from_ep0_ptr   ; inline asm is opaque to the compiler,
                                        ; so declare the callee by hand
            lcall _dptr_from_ep0_ptr
            mov   a,#0x02
            movx  @dptr,a
        __endasm;
    } else {
        __asm
            lcall _dptr_from_ep0_ptr
            mov   a,#0x01
            movx  @dptr,a
        __endasm;
    }
    ep0_send_1byte();
}
