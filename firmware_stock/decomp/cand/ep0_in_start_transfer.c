// MATCH: image=rev20 addr=0x0B77 len=11 func=ep0_in_start_transfer cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void ep0_in_fill_chunk(void);
/* Fill the first packet, then clear the NACK bit (7) so the UBM will answer
 * the next IN token. */
void ep0_in_start_transfer(void) {
    ep0_in_fill_chunk();
    IEPDCNTX0 &= 0x7F;
}
