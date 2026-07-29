// MATCH: image=rev20 addr=0x0B5F len=15 func=ep0_nack_both cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* NACK both directions of EP0 and drop any expected data stage. */
void ep0_nack_both(void) { IEPDCNTX0 = 0x80; OEPDCNTX0 = 0x80; f_stage_out = 0; f_stage_in = 0; }
