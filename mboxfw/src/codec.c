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
 * RAM[0x25] is the codec-state byte read by codec_state_adjust(). */
static __data unsigned char g_codec_state_25;

/*
 * Port of Rev 20 fcn.0x0E62 (entry mid-function at rev20_flat.asm:1885).
 *
 * NOT a codec bit-serial write — that lives at fcn.0x0E74 (P1.0 data,
 * P1.2 clock, 8+8 bit shift with P1.1 latch). fcn.0x0E62 is a pure
 * state-machine adjuster: it forces RAM[0x22] bit 2, then computes
 * RAM[0x22] bit 6 from RAM[0x25] bits 4 and 5.
 *
 * The tail logic at 0x0E64..0x0E73 collapses to:
 *   bit 22.6 = (bit 25.4 == 0) && (bit 25.5 == 0)
 * i.e. only both-clear leaves the mux P1.6 line asserted.
 */
static void codec_state_adjust(void)
{
    g_mux_state |= (unsigned char)0x04;              /* setb 0x22.2 */

    if (g_codec_state_25 & 0x10) {                   /* 0x25.4 */
        g_mux_state &= (unsigned char)~0x40;         /* clr 0x22.6 */
    } else {
        g_mux_state |= (unsigned char)0x40;          /* setb 0x22.6 */
    }
    if (g_codec_state_25 & 0x20) {                   /* 0x25.5 */
        g_mux_state &= (unsigned char)~0x40;         /* clr 0x22.6 */
    }
    /* Rev 20's fcn.0x0970 does NOT re-push the mux after the adjuster —
     * the newly-set bits 22.2 / 22.6 stay latched in RAM until the next
     * mux_write triggered by a control-change event. Preserve that
     * behavior: no mux_write here. */
}

void codec_init(void)
{
    /* fcn.0x0970: clr 0x22.0, clr 0x22.3, clr 0x23.6 */
    g_mux_state    &= (unsigned char)~0x01;   /* bit 0: mux data-A */
    g_mux_state    &= (unsigned char)~0x08;   /* bit 3: mux mode select */
    g_phantom_48v   = 0;                      /* RAM[0x23].6 */

    /* fcn.0x0970 next: lcall 0x0F0C — kick the mux to publish RAM[0x22]. */
    mux_write(g_mux_state);

    /* fcn.0x0970 tail: clear the codec-state byte, then run the state
     * adjuster (Rev 20 does lcall 0x0E62 here). */
    g_codec_state_25 = 0;
    codec_state_adjust();
}
