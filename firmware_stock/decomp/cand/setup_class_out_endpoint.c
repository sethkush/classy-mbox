// MATCH: image=rev20 addr=0x006B len=8 func=setup_class_out_endpoint cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* bmRequestType 0x22: class, host->device, endpoint. Tag it and expect data. */
void setup_class_out_endpoint(void) { g_class_tag = 1; f_stage_out = 1; f_stage_in = 0; }
