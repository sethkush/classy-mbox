// MATCH: image=rev20 addr=0x015D len=22 func=std_get_configuration cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void ep0_ptr_set_in_buf(void);
extern void ep0_send_1byte(void);
__bit __at (0x0E) f_configured;
/* Returns 1 if configured, 0 if not. Note the zero arm uses CLR A rather than
 * MOV A,#0 -- one byte instead of two, and what the original emitted. */
void std_get_configuration(void) {
    ep0_ptr_set_in_buf();
    if (f_configured) {
        __asm
            .globl _dptr_from_ep0_ptr
            lcall _dptr_from_ep0_ptr
            mov   a,#0x01
            movx  @dptr,a
        __endasm;
    } else {
        __asm
            lcall _dptr_from_ep0_ptr
            clr   a
            movx  @dptr,a
        __endasm;
    }
    ep0_send_1byte();
}
