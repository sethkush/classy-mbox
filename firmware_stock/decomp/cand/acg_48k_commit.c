// MATCH: image=rev20 addr=0x0DEB len=44 func=sfr_store_then_acg_48k span=1 defines=acg_48k_commit cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Ghidra splits this at 0x0E0F because a second caller jumps into the tail.
 * At source level it is one function: load both synthesizers with the
 * 48 kHz-family word 0x61A80F, then commit via ACGCTL. */
/* Entry point one byte earlier, at 0x0DEB, for callers that are still holding
 * a pending byte in A: the MOVX commits it to the caller's DPTR and then falls
 * straight through into the 48 kHz commit below.
 *
 * Written as a __naked function with no RET so that control runs on into the
 * next function SDCC emits. That relies on definition order within the
 * translation unit, which is the same thing that makes the merged tail in
 * setup_get_sample_freq work -- and, as there, the byte match is what proves
 * the two really are adjacent. `defines=` in the header tells link51 this one
 * candidate owns both labels, so the prologue byte is covered rather than
 * left to a symbols.map equate. */
void sfr_store_then_acg_48k(void) __naked {
    __asm
        movx  @dptr,a          ; commit the caller's pending byte
    __endasm;
}

void acg_48k_commit(void) {
    ACG1FRQ1 = 0xA8; ACG1FRQ2 = 0x61; ACG1FRQ0 = 0x0F;
    ACG2FRQ1 = 0xA8; ACG2FRQ2 = 0x61; ACG2FRQ0 = 0x0F;
    ACGCTL = 0x06;
}
