/*
 * Front-panel button poller.
 * Ports Rev 20's fcn.0x0ED5.
 *
 * Three buttons on P3 (active-low, internal pull-up):
 *   P3.3 = channel 1 source cycle (mic → line → inst)
 *   P3.4 = channel 2 source cycle (mic → line → inst)
 *   P3.5 = 48V phantom power toggle
 */

#include "regs.h"
#include "buttons.h"
#include "mux.h"
#include "codec.h"

/* Rev 20 stores the previous button-state snapshot in RAM[0x20]. */
static __data unsigned char prev_p3 = 0xFF;

/* Cycle a 3-bit source-select field through mic → line → inst → mic.
 * Rev 20 uses these patterns (see fcn.0x0E27 / fcn.0x0E9D):
 *   Mic:  1 0 1   Line: 1 1 0   Inst: 0 1 1
 */
static unsigned char cycle_source(unsigned char cur, unsigned char shift)
{
    unsigned char pat = (cur >> shift) & 0x07;
    unsigned char next;
    switch (pat) {
        case 0x05: next = 0x06; break;   /* mic → line */
        case 0x06: next = 0x03; break;   /* line → inst */
        default:   next = 0x05; break;   /* inst (or anything) → mic */
    }
    return (cur & ~(0x07 << shift)) | (next << shift);
}

void buttons_poll(void)
{
    unsigned char now = P3;
    unsigned char changed = now ^ prev_p3;
    unsigned char pressed_low = changed & ~now;   /* falling edges only */

    /* codec_commit() runs the state adjuster, re-writes the mux, and
     * shifts the resulting 16-bit codec control word — so any state
     * bit we flip (mux or codec_state_23/25) gets published to hardware
     * in one call. Matches Rev 20's control-change idiom which routes
     * through fcn.0x0E62 → fcn.0x0E74. */
    if (pressed_low & P3_BTN_CH1_MASK) {
        g_mux_state = cycle_source(g_mux_state, 0);
        codec_commit();
    }
    if (pressed_low & P3_BTN_CH2_MASK) {
        g_mux_state = cycle_source(g_mux_state, 3);
        codec_commit();
    }
    if (pressed_low & P3_BTN_48V_MASK) {
        g_phantom_48v = !g_phantom_48v;
        codec_commit();
    }

    prev_p3 = now;
}
