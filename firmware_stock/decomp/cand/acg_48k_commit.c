// MATCH: image=rev20 addr=0x0DEC len=43 func=acg_48k_commit cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Ghidra splits this at 0x0E0F because a second caller jumps into the tail.
 * At source level it is one function: load both synthesizers with the
 * 48 kHz-family word 0x61A80F, then commit via ACGCTL. */
void acg_48k_commit(void) {
    ACG1FRQ1 = 0xA8; ACG1FRQ2 = 0x61; ACG1FRQ0 = 0x0F;
    ACG2FRQ1 = 0xA8; ACG2FRQ2 = 0x61; ACG2FRQ0 = 0x0F;
    ACGCTL = 0x06;
}
