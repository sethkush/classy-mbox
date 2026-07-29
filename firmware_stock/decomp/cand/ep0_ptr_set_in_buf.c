// MATCH: image=rev20 addr=0x0B3E len=7 func=ep0_ptr_set_in_buf cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Point the EP0 working pointer at the IN buffer, XDATA 0xFA18. */
void ep0_ptr_set_in_buf(void) { g_ep0_ptr_hi = 0xFA; g_ep0_ptr_lo = 0x18; }
