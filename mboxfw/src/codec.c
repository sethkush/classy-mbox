/*
 * Codec / audio-path state initialiser — port of Rev 20 fcn.0x0970.
 *
 * The codec + input mux + phantom-power state is held in three IRAM bytes:
 *   RAM[0x22] — mux bits (74HC595 payload)
 *   RAM[0x23] — codec-serial payload (MSB byte of the 16-bit shift word)
 *   RAM[0x25] — mode flags used by the state adjuster at fcn.0x0E62
 *
 * Rev 20 zeroes those bytes, pushes the (now-zero) mux state out through
 * the 74HC595 (mux_update() ~= fcn.0x0F0C), then runs the codec state
 * adjuster to sync RAM[0x22]/[0x25] into a consistent "everything off"
 * baseline. Actual codec init happens on-demand from SET_CUR handlers
 * later — this is just the reset baseline.
 *
 * The three state bytes are private to Rev 20's bit-serial routines and
 * don't need to be exposed as globals; we mirror them by re-implementing
 * the same idiom in-place. Because we currently don't have the fcn.0x0E62
 * codec-state adjuster ported, the last-step "propagate to codec" is a
 * TODO — for the first-light attempt we rely on the codec latching zeros
 * from the mux clock and treating that as its idle state.
 *
 * Reference: firmware_stock/disasm/rev20_flat.asm lines 1193–1201.
 */

#include "regs.h"
#include "mux.h"
#include "codec.h"

/* Rev 20 RAM[0x22] is g_mux_state (mux.c); RAM[0x23].6 is g_phantom_48v.
 * RAM[0x25] is codec state we don't propagate anywhere yet. */
static __data unsigned char g_codec_state_25;

void codec_init(void)
{
    /* fcn.0x0970: clr 0x22.0, clr 0x22.3, clr 0x23.6 */
    g_mux_state    &= (unsigned char)~0x01;   /* bit 0: mux data-A */
    g_mux_state    &= (unsigned char)~0x08;   /* bit 3: mux mode select */
    g_phantom_48v   = 0;                      /* RAM[0x23].6 */

    /* fcn.0x0970 next: lcall 0x0F0C — kick the mux to publish RAM[0x22]. */
    mux_write(g_mux_state);

    /* fcn.0x0970 tail: clear codec-state byte and re-invoke the state
     * adjuster (fcn.0x0E62). We don't have the adjuster ported yet;
     * clearing to zero + one mux kick matches the "everything idle"
     * baseline that Rev 20 lands on for the first bus-reset. */
    g_codec_state_25 = 0;
    /* TODO: cs_state_adjust();  // port of fcn.0x0E62 (16-bit shift + latch) */
}
