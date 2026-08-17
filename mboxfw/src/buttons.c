/*
 * Front-panel button poller.
 * Ports Rev 20 fcn.0x0ED5 / Rev 22 fcn.0x0F31.
 *
 * Three momentary buttons on P3 (ACTIVE HIGH -- the board pulls them low at
 * rest and a press drives them high; the internal P3 pull-ups must be off,
 * GLOBCTL P3PUDIS, or the pins read a stuck 1 and nothing here ever fires.
 * See firmware_stock/decomp/FINDING_buttons_are_active_high.md):
 *   P3.3 = channel 1 source cycle   -> Rev 20 fcn.0x0E27 / Rev 22 fcn.0x0E1B
 *   P3.4 = channel 2 source cycle   -> Rev 20 fcn.0x0E9D / Rev 22 fcn.0x0E8F
 *   P3.5 = mono fold-down toggle    -> Rev 20 fcn.0x1028 / Rev 22 fcn.0x1020
 *
 * P3.5 was documented here as "48V phantom power" for most of the project.
 * It is not; see the g_mono comment in mux.h. 48V is a mechanical switch with
 * no firmware bit.
 */

#include "regs.h"
#include "buttons.h"
#include "mux.h"
#include "codec.h"
#include "usb.h"   /* #207 usb_status_notify() */

/* Rev 20 stores the previous button-state snapshot in RAM[0x20]. */
static __data unsigned char prev_p3 = 0x00;

/* Seeded to 0x00, matching stock: Keil's ?C_INITSEG table zeroes the shadow at
 * IRAM 0x20 (cand/c51_initseg_table.c, record `01 20 00`). That zero is also
 * the proof of the polarity -- the edge test is prev==0 && cur==1, so if these
 * pins idled HIGH all three stock handlers would fire on the first scan of
 * every boot and the box would come up on LINE with mono flipped. It comes up
 * on MIC. This was 0xFF while the code believed the buttons were active-low. */

/*
 * Cycle a 3-bit source-select field: mic -> line -> inst -> mic.
 *
 * The three legal patterns and the order are both taken from the stock
 * handlers, which write the field bit-by-bit rather than as a byte:
 *
 *   Rev 20 fcn.0x0E27, channel 1 (bits 0x10/0x11/0x12 = RAM[0x22].0/.1/.2)
 *     0x0E2E-0x0E32   b0=1 b1=0 b2=1   -> 0x05
 *     0x0E40-0x0E44   b0=1 b1=1 b2=0   -> 0x03
 *     0x0E4C-0x0E50   b0=0 b1=1 b2=1   -> 0x06
 *   Rev 20 fcn.0x0E9D, channel 2 (bits 0x13/0x14/0x15 = RAM[0x22].3/.4/.5)
 *     0x0EA4-0x0EA8 -> 0x05,  0x0EB3-0x0EB7 -> 0x03,  0x0EBF-0x0EC3 -> 0x06
 *   Rev 22 the same at 0x0E22/0x0E34/0x0E40 and 0x0E96/0x0EA5/0x0EB1.
 *
 * Stock does not derive the next pattern from the current one — it keeps a
 * 2-bit state per channel in RAM[0x25] (ch1: bits 0x28/0x2A = 0x25.0/.2;
 * ch2: bits 0x29/0x2B = 0x25.1/.3) and emits the pattern for the new state:
 *
 *   Rev 20 0x0E27  JB 0x28 -> ...      (0,0) -> set both  -> 0x05
 *          0x0E36  JNB 0x28 / JNB 0x2A (1,1) -> set,clear -> 0x03
 *          0x0E48                      (1,0) -> clear both -> 0x06
 *
 * so the walk is (0,0)->0x05->0x03->0x06->0x05. Boot leaves the state at
 * (0,0) with the field already at 0x06 (Rev 20 hw_init @ 0x095B writes
 * RAM[0x22] = 0xFF then clears .0 and .3, giving 0xF6 = 6 on both channels;
 * Rev 22 @ 0x087C). Seth reports the hardware boots to MIC and the button
 * walks mic -> line -> inst, which pins the mapping:
 *
 *   0x06 = MIC   (boot)     0x05 = LINE  (1st press)     0x03 = INST  (2nd)
 *
 * This file previously asserted 0x05=mic / 0x06=line — mic and line swapped —
 * AND cycled 0x05->0x06->0x03, swapping the middle two positions. The two
 * errors did not cancel: mboxfw walked mic -> inst -> line.
 *
 * The pattern-driven form below is equivalent to stock's state machine as
 * long as the field only ever holds one of the three legal patterns, which
 * holds because hw_init seeds 0x06 and nothing else writes the field.
 */
static unsigned char cycle_source(unsigned char cur, unsigned char shift)
{
    unsigned char pat = (cur >> shift) & 0x07;
    unsigned char next;
    switch (pat) {
        case 0x06: next = 0x05; break;   /* mic  → line */
        case 0x05: next = 0x03; break;   /* line → inst */
        default:   next = 0x06; break;   /* inst (or anything) → mic */
    }
    return (cur & ~(0x07 << shift)) | (next << shift);
}

void buttons_poll(void)
{
    unsigned char now = P3;
    unsigned char changed = now ^ prev_p3;
    /*
     * Stock acts on the RISING edge — i.e. on button PRESS, since the buttons
     * are active HIGH. Rev 20 fcn.0x0ED5 @ 0x0EE0 for mono:
     *
     *   0ee0  JB 0x05,0x0eed     ; bit 0x05 = RAM[0x20].5 = PREVIOUS P3.5
     *   0ee3  MOV A,R5           ; R5 = P3, sampled at 0x0ED7
     *   0ee4  JNB ACC.5,0x0eed   ; current P3.5
     *   0ee7  LCALL 0x1028
     *
     * Both guards must pass, so the handler runs only when prev = 0 and
     * cur = 1. Same shape at 0x0EED (P3.3) and 0x0EFA (P3.4); Rev 22 at
     * 0x0F3A / 0x0F47 / 0x0F54.
     *
     * This was `changed & ~now` — the wrong edge. The expression below has
     * been correct since that fix; only the name and the reasoning attached to
     * it were wrong. It was called a release because the buttons were believed
     * to be active-low. They are active high, so low->high is the press, and
     * stock therefore acts on the DOWN stroke with no debounce.
     */
    unsigned char pressed = changed & now;
    unsigned char acted = 0;

    if (pressed & P3_BTN_CH1_MASK) {
        g_mux_state = cycle_source(g_mux_state, 0);
        acted = 1;
    }
    if (pressed & P3_BTN_CH2_MASK) {
        g_mux_state = cycle_source(g_mux_state, 3);
        acted = 1;
    }
    if (pressed & P3_BTN_MONO_MASK) {
        /* Rev 20 fcn.0x1028 / Rev 22 fcn.0x1020: a bare toggle of bit 0x1E,
         * nothing else. */
        MONO_SET(!MONO_IS_SET());
        acted = 1;
    }

    prev_p3 = now;

    if (acted) {
        /*
         * Publish ONCE, both words, in stock's order: panel first, then the
         * 16-bit codec word. Rev 20's main loop @ 0x0AE3-0x0AE9:
         *
         *   0ae3  JNB ACC.0,0x0aec   ; the handler's "I acted" return flag
         *   0ae6  LCALL 0x0F0C       ; panel/mux word  RAM[0x22]
         *   0ae9  LCALL 0x0E62       ; codec word      RAM[0x23]:RAM[0x25]
         *
         * Rev 22 @ 0x0A8D-0x0A93. The handler returns a flag (R6, OR-ed with
         * 1 at each of 0x0EEA/0x0EF7/0x0F04) precisely so the loop can skip
         * both publishes when nothing changed.
         *
         * This file used to call codec_commit() inside each of the three
         * branches, so pressing two buttons in one poll published twice.
         */
        codec_source_changed();
        mux_write(g_mux_state);
        codec_write_word();

        /* #207. Tell the host the source moved under it. Without this a
         * front-panel press is invisible until something makes the host poll
         * again, so ALSA and Core Audio keep showing the previous input --
         * which is exactly the mismatch that voided a measurement session on
         * 2026-07-29, in the other direction.
         *
         * Names the Selector Unit, because that is the control whose value
         * changed as far as a host is concerned: #203 made its GET_CUR report
         * the PUBLISHED mux state, so the host reads the truth when it asks. */
        /* #228: REMOVED. The panel selects the analog front end, which the
         * host's Selector Unit no longer reports -- it reports analog vs
         * S/PDIF, and a button cannot change that. Notifying here would prompt
         * a GET_CUR for a value that has not moved. The status endpoint stays
         * declared and armed for controls that CAN change behind the host's
         * back; nothing on this device currently does. */
    }
}
