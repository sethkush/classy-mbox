// MATCH: image=rev20 addr=0x1001 func=dma0_disable cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "regs.h"
void dma0_disable(void) { DMACTL0 &= 0x7F; }
