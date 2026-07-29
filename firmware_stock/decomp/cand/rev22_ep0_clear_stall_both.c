// MATCH: image=rev22 addr=0x0B3E len=15 func=ep0_clear_stall_both cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Clear the STALL bit (3) on both halves of endpoint 0, leaving the data
 * toggle (bit 5) alone -- 0xF7 == ~0x08. This is the CLEAR_FEATURE(ENDPOINT_
 * HALT) / bus-reset recovery path, as opposed to ep0_clear_stall_toggle
 * (0x0B4D) which uses 0xD7 and resets the toggle as well.
 *
 * REV 20 -> REV 22 DELTA: byte-identical to rev20 0x0B50; only the address
 * moved. */
void ep0_clear_stall_both(void) { IEPCNF0 &= 0xF7; OEPCNF0 &= 0xF7; }
