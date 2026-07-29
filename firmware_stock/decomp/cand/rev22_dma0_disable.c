// MATCH: image=rev22 addr=0x0FF2 len=8 func=dma0_disable cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Rev 22 counterpart of cand/dma0_disable.c (rev20 @0x1001).
 *
 * Clears DMACTL0.7 (DMAEN, TAS1020B DMA channel 0 enable) leaving the other
 * seven bits alone, so the caller can reprogram the audio path without the
 * DMA engine walking the buffer underneath it.
 *
 * Byte-identical to the Rev 20 function, only relocated:
 *   rev20 0x1001: 90 ff e8 e0 54 7f f0 22
 *   rev22 0x0FF2: 90 ff e8 e0 54 7f f0 22
 * The C source is reused verbatim from the Rev 20 candidate; only the MATCH
 * header changed.  Both images call it from cmd1_apply_clock_mode
 * (rev20 0x0344, rev22 0x0336) among others. */
void dma0_disable(void) { DMACTL0 &= 0x7F; }
