// MATCH: image=rev22 addr=0x0B63 len=11 func=ep0_in_stage_and_go cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void ep0_in_send_chunk(void);
/* Start an EP0 IN data stage: stage the first packet, then clear NAK (bit 7)
 * of IEPDCNTX0 so the UBM answers the next IN token with it. Subsequent
 * packets are staged and un-NAKed from the IEP0 interrupt path.
 *
 * REV 20 -> REV 22 DELTA: byte-identical in effect to rev20
 * ep0_in_start_transfer at 0x0B77 (11 bytes, same encoding); only the two
 * addresses moved -- itself and the LCALL target, rev20 0x0B8C -> rev22
 * 0x0ABB. */
void ep0_in_stage_and_go(void) {
    ep0_in_send_chunk();
    IEPDCNTX0 &= 0x7F;
}
