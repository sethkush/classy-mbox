// MATCH: image=rev20 addr=0x0B50 len=15 func=ep0_clear_stall_both cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Clear the STALL bit (3) on both halves of endpoint 0. */
void ep0_clear_stall_both(void) { IEPCNF0 &= 0xF7; OEPCNF0 &= 0xF7; }
