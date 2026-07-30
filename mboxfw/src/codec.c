/*
 * Codec control word — Rev 20 fcn.0x0E62 (16-bit serial write) plus the
 * source-cycle tail at 0x0E52. See codec.h for the routine-identification
 * correction that this file used to get backwards.
 *
 * State lives in three IRAM bytes on stock:
 *   RAM[0x22] — panel/mux shift word (8 bits + mono as a 9th), mux_write()
 *   RAM[0x23] — codec control-word HIGH byte (shifted out first)
 *   RAM[0x25] — codec control-word LOW byte
 *
 * Mirrored here as g_mux_state (mux.c), g_codec_state_23, g_codec_state_25.
 *
 * Wire protocol, from the loop at Rev 20 0x0E68-0x0E9C:
 *   P1.0 = data      (ORL P1,#0x01 / ANL P1,#0xFE)
 *   P1.2 = clock     (ORL P1,#0x04 then ANL P1,#0xFB — rising edge samples)
 *   P1.1 = latch     (ORL P1,#0x02 then ANL P1,#0xFD, once, at the end)
 *
 * Note this is a DIFFERENT three pins from the panel shift register, which
 * uses P1.7/P1.5/P1.6 (mux.c). Two independent shift chains on one port.
 */

#include "regs.h"
#include "mux.h"
#include "codec.h"

__data unsigned char g_codec_state_23 = 0;
__data unsigned char g_codec_state_25 = 0;

void codec_source_changed(void)
{
    /* 0x22.6 = !(0x25.4) && !(0x25.5) — see codec.h for the byte sequence.
     *
     * What this file previously called codec_state_adjust() also did
     * `g_mux_state |= 0x04`, described as "setb 0x22.2", and ran on every
     * publish. There is no such unconditional write in either stock image:
     * bit 0x12 (= 0x22.2) is set at Rev 20 0x0E32 and 0x0E50 and cleared at
     * 0x0E4C, which are the three source-PATTERN emissions — b2 of the
     * channel-1 field, not a separate control line.
     *
     * Forcing it set was not cosmetic. Pattern 0x03 (INST) has b2 = 0, so
     * every publish rewrote 0x03 to 0x07 — not one of the three legal
     * patterns — and instrument input could never actually be selected. */
    if ((g_codec_state_25 & 0x10) || (g_codec_state_25 & 0x20)) {
        g_mux_state &= (unsigned char)~0x40;
    } else {
        g_mux_state |= (unsigned char)0x40;
    }
}

/*
 * Shift one byte out on P1.0, MSB first, clocked by a rising then falling
 * edge on P1.2. Rev 20's rotate loop at 0x0E6B-0x0E89 decodes to this.
 */
static void codec_shift_byte(unsigned char b)
{
    unsigned char i;
    unsigned char v = b;

    for (i = 0; i < 8; i++) {
        if (v & 0x80) {
            P1 |= P1_CODEC_SDIN_MASK;      /* orl 0x90, #0x01 */
        } else {
            P1 &= (unsigned char)~P1_CODEC_SDIN_MASK; /* anl 0x90, #0xfe */
        }
        P1 |= P1_CODEC_SCLK_MASK;          /* orl 0x90, #0x04 (clock rise) */
        P1 &= (unsigned char)~P1_CODEC_SCLK_MASK; /* anl 0x90, #0xfb (fall) */
        v <<= 1;
    }
}

void codec_write_word(void)
{
    /* Stock uses RAM[0x26].0 as a "which byte am I on" flag inside one loop
     * (SETB 0x30 at 0x0E66, tested at 0x0E8B, cleared at 0x0E8E). Two calls
     * emit the same signal without the flag. */
    codec_shift_byte(g_codec_state_23);   /* high byte first */
    codec_shift_byte(g_codec_state_25);   /* low  byte second */

    P1 |= P1_CODEC_LATCH_MASK;             /* orl 0x90, #0x02 */
    P1 &= (unsigned char)~P1_CODEC_LATCH_MASK; /* anl 0x90, #0xfd */
}

void codec_init(void)
{
    /* Rev 20 fcn.0x08CB @ 0x0967-0x096C, Rev 22 fcn.0x07EC @ 0x0888-0x088D:
     * zero both bytes and publish. Publishing 0x0000 turns every bit of the
     * word off, which is consistent with the word being active-high
     * throughout — the suspend path at Rev 20 0x0533 does exactly the same
     * two stores and the same LCALL 0x0E62 to shut the audio path down.
     *
     * This function used to end with the bit-fiddling tail and NO shift,
     * on the reasoning that "0x0E62 is the adjuster and does not chain into
     * 0x0E74". 0x0E62 is the shift itself, so init did publish on stock and
     * mboxfw was silently skipping it. */
    g_codec_state_23 = 0;
    g_codec_state_25 = 0;
    codec_write_word();
}
