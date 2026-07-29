// MATCH: image=rev20 addr=0x010D len=11 func=send_3byte_ep0_reply cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
void send_3byte_ep0_reply(void) { IEPDCNTX0 = 3; f_stage_out = 0; f_stage_in = 1; }
