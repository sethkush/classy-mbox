// MATCH: image=rev20 addr=0x0B45 len=11 func=ep0_send_1byte cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
void ep0_send_1byte(void) { IEPDCNTX0 = 1; f_stage_out = 0; f_stage_in = 1; }
