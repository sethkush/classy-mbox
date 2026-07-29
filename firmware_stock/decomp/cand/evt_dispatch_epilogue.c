// MATCH: image=rev20 addr=0x0564 len=4 func=evt_dispatch_epilogue cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
void evt_dispatch_epilogue(void) { g_event = 0; }
