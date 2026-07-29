// MATCH: image=rev20 addr=0x0E20 len=7 func=queue_chip_reg4_val40 cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Stage CS8427 register 4 = 0x40 for the clock-mode tail to write out. */
void queue_chip_reg4_val40(void) { g_chip_reg = 4; g_chip_val = 0x40; }
