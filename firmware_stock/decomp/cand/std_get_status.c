// MATCH: image=rev20 addr=0x022F len=30 func=std_get_status cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void ep0_ptr_set_in_buf(void);
extern void ep0_buf_clear_byte(void);   /* takes the low address byte in A */

/* GET_STATUS, for every recipient: always answers 0x0000.
 *
 * Nothing is examined -- not bmRequestType, not wIndex -- so the device
 * reports itself bus-powered, not remote-wakeup-capable, and no endpoint ever
 * halted, whatever was asked. That last part is not merely lazy: std_set_feature
 * and std_clear_feature do maintain a halt state, and this handler will not
 * report it. A host that halts an endpoint and then reads the status back sees
 * zero.
 *
 * Two bytes, both zero, built the same way as every other EP0 reply: store
 * through the working pointer, advance it, then let ep0_buf_clear_byte take
 * the second byte from the low address left in A. */
void std_get_status(void) {
    ep0_ptr_set_in_buf();
    __asm
        .globl _dptr_from_ep0_ptr
        lcall _dptr_from_ep0_ptr
        clr   a
        movx  @dptr,a
    __endasm;
    ++g_ep0_ptr_lo; if (g_ep0_ptr_lo == 0) ++g_ep0_ptr_hi;
    ep0_buf_clear_byte();

    IEPDCNTX0 = 2;
    f_stage_out = 0;
    f_stage_in = 1;
}
