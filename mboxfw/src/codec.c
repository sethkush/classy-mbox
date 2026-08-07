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

/* hw_init.c owns the ~4000-cycle settle spin; #197 borrows it rather than
 * carrying a second copy. Declared here in the style main.c uses for hw_init()
 * -- there is no hw_init.h. */
extern void hw_short_delay(void);

__data unsigned char g_codec_state_23 = 0;
__data unsigned char g_codec_state_25 = 0;

/*
 * Mirror the per-channel source selection into the CODEC word's low nibble.
 * #170.
 *
 * Stock keeps a 2-bit state per channel in RAM[0x25] and derives the 3-bit
 * panel pattern from it; mboxfw keeps the pattern (g_mux_state) and derives
 * the state here. The two forms are equivalent because the pattern is only
 * ever one of the three legal one-cold values — cycle_source() emits nothing
 * else, handle_set_mux() rejects anything else, and hw_init() seeds 0x06.
 *
 * The state machines, identical instruction-for-instruction in both images:
 *
 *   Rev 20 button_a_cycle_3state @0x0E27   Rev 22 panel_state_cycle_A @0x0E1B
 *     @0x0E2A SETB 0x28 / SETB 0x2A        @0x0E1E SETB 0x28 / SETB 0x2A
 *     @0x0E3C SETB 0x28 / CLR  0x2A        @0x0E30 SETB 0x28 / CLR  0x2A
 *     @0x0E48 CLR  0x28 / CLR  0x2A        @0x0E3C CLR  0x28 / CLR  0x2A
 *   Rev 20 button_b_cycle_3state @0x0E9D   Rev 22 panel_state_cycle_B @0x0E8F
 *     @0x0EA0 SETB 0x29 / SETB 0x2B        @0x0E92 SETB 0x29 / SETB 0x2B
 *     @0x0EAF SETB 0x29 / CLR  0x2B        @0x0EA1 SETB 0x29 / CLR  0x2B
 *     @0x0EBB CLR  0x29 / CLR  0x2B        @0x0EAD CLR  0x29 / CLR  0x2B
 *
 * Bit addresses 0x28..0x2B are RAM[0x25].0..3: ch1 = (.0 lo, .2 hi), ch2 =
 * (.1 lo, .3 hi). Pairing each branch with the panel pattern it emits in the
 * same basic block gives the map:
 *
 *   pattern 0x06 MIC  (boot) -> (lo,hi) = (0,0)
 *   pattern 0x05 LINE        -> (1,1)
 *   pattern 0x03 INST        -> (1,0)
 *
 * Until this existed the low nibble was write-zero-only, so the codec chain
 * said MIC on both channels no matter what the panel/relay chain said. Boot is
 * unaffected — MIC maps to (0,0), which is the value codec_init() publishes.
 */
static unsigned char src_state(unsigned char pat)
{
    if (pat == 0x05) { return 0x03; }   /* LINE -> lo=1 hi=1 */
    if (pat == 0x03) { return 0x01; }   /* INST -> lo=1 hi=0 */
    return 0x00;                        /* MIC  -> lo=0 hi=0 */
}

void codec_source_changed(void)
{
    unsigned char s1 = src_state(g_mux_state & 0x07);
    unsigned char s2 = src_state((unsigned char)(g_mux_state >> 3) & 0x07);

    /* Clear the four state bits, then set them individually with literal
     * masks. Written this way rather than as one compound RMW expression for
     * two reasons: it is the shape stock uses (SETB/CLR per bit, never a byte
     * store), and `tools/latch_word_bit_diff.py` can read literal `|=` masks
     * exactly. A compound `x = (x & ~0x0F) | <expr>` forces that gate onto its
     * conservative not-a-pure-literal branch, which masks the byte to 0xFF and
     * would report 0x25.4/0x25.5 as driven when nothing drives them. */
    g_codec_state_25 &= (unsigned char)~0x0Fu;
    if (s1 & 0x01u) { g_codec_state_25 |= CODEC25_SRC1_LO; }  /* 0x25.0 */
    if (s1 & 0x02u) { g_codec_state_25 |= CODEC25_SRC1_HI; }  /* 0x25.2 */
    if (s2 & 0x01u) { g_codec_state_25 |= CODEC25_SRC2_LO; }  /* 0x25.1 */
    if (s2 & 0x02u) { g_codec_state_25 |= CODEC25_SRC2_HI; }  /* 0x25.3 */

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

/* #190. Bits the host has muted, in CODEC23_ positions. Zero = nothing muted,
 * which is the boot state and the behaviour every build before 0x0036 had. */
__data unsigned char g_host_mute = 0;
__data unsigned char g_path_enabled = 0;

void codec_apply_mute(void)
{
    /* The pair is a pure function of two pieces of state, and this is the ONLY
     * writer of those two bits. Splitting "is the path up" from "has the host
     * muted it" is what keeps the two independent: the alternative -- reading
     * the current pair back and guessing -- cannot tell a path that is down
     * from one that is muted, so unmute would either fail to restore or would
     * switch on a path no stream had opened.
     *
     * NOVEL — reason: stock has no host-settable mute, so there is no stock
     * sequence to cite. The published VALUES are stock's own: the pair is
     * exactly the field Rev 20 fcn.0x0728 clears at 0x072F/0x0731 and sets at
     * 0x07EE (Rev 22 fcn.0x0763 @ 0x076A/0x076C and 0x082A), and the publish
     * path is codec_write_word(), which is stock's LCALL 0x0E62. */
    /* Raise both, then mask down. One `|=` with a LITERAL operand and two
     * `&=`, which is both the cheapest form and the one the bit-coverage gate
     * can read: latch_word_bit_diff.py resolves the settable mask from the C
     * source and must over-estimate rather than under, so an RHS mentioning
     * any identifier is credited with all eight bits -- silently turning
     * 0x23.0/0x23.1 from documented gaps into apparently-driven ones. A `&=`
     * cannot set a bit, so the gate skips it and the literal `|=` is the whole
     * truth about what this can raise. The gate caught the byte-wide version
     * twice in two days, here and in the #189 bench handler. */
    g_codec_state_23 |= (unsigned char)CODEC23_MUTE_PAIR_ALL;
    g_codec_state_23 &= (unsigned char)~g_host_mute;
    g_codec_state_23 &= (unsigned char)(g_path_enabled
                                        | (unsigned char)~CODEC23_MUTE_PAIR_ALL);
    codec_write_word();
}

void codec_clear_adc_transient(void)
{
    /* #197. One pulse of the capture gate, at boot, removes the ADC start-up
     * transient permanently -- measured on unit B, which went from -38.9 dBFS
     * in the first 100 ms of a capture to -101.1, and stayed clear across
     * repeated stream starts, a 44.1/48 rate change, 60 s idle and a USB bus
     * reset. Only a power cycle re-arms it. FINDING_197.
     *
     * Why a stream start does not already do this: streaming_set_rate() does
     * `|=` on a bit that is already set, so its publish writes an unchanged
     * value and the bit never moves. The host mute path drives it 1->0->1 with
     * a publish each way, and it is that transition, not the write, that
     * clears the transient -- codec_apply_mute()'s own write was measured to
     * produce no transient at all.
     *
     * NOVEL — reason: neither stock image does this. Stock raises the pair
     * once at power-up (Rev 20 0x080B-0x0852, Rev 22 0x09B6-0x09F5,
     * byte-identical bit order) and never pulses it, and its per-stream path
     * (Rev 20 0x0395-0x03BD, Rev 22 0x0399-0x03C1) contains no mute and no
     * long delay. So the transient is original Mbox 1 behaviour and this is an
     * improvement over stock rather than a repair -- which is exactly why it
     * gets a NOVEL tag and not a citation. FINDING_196 has the stock table.
     *
     * TWO THINGS UNVERIFIED, both needing a unit that still has the transient,
     * and both units were cleared by the experiment that found this:
     *   - the minimum hold; the measurement held the gate low for 4 s, and the
     *     ~860 ms below is chosen as ~5 tau of the 171 ms settling constant
     *     rather than because a shorter one was shown to fail
     *   - whether boot is early enough, with the codec freshly up, rather than
     *     later with everything running as in the measurement
     * The replug that flashing requires re-arms the transient, so the check is
     * free: capture immediately afterwards and the first 100 ms should sit at
     * the -101 to -105 dBFS floor rather than -39. If it does not, move this
     * call to the first stream open instead of boot. */
    unsigned char n;

    /* Literal operand, for the reason codec_apply_mute() spells out: a `|=`
     * whose RHS mentions any identifier is credited with all eight bits by
     * latch_word_bit_diff.py, which would turn 0x23.0/0x23.1 from documented
     * gaps into apparently-driven ones. */
    g_codec_state_23 |= (unsigned char)CODEC23_MUTE_PAIR_ALL;
    codec_write_word();

    /* hw_short_delay() rather than a local loop: it already carries the
     * volatile counter that stops SDCC deleting the call site, and a second
     * copy of the body cost 15 bytes in an image with none spare. */
    for (n = 0; n < 48; n++) {
        hw_short_delay();
    }

    /* NOT a blind clear. A SET_INTERFACE can land during the hold above --
     * usb_init() runs first (task #47), so the host is enumerating throughout
     * boot -- and clearing the pair unconditionally would switch the capture
     * path off underneath a stream that had just opened. codec_apply_mute()
     * recomputes the pair from g_path_enabled and g_host_mute, so it lands on
     * the right value whether or not that happened. */
    codec_apply_mute();
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
    /* The audio path is down until a stream opens; g_host_mute deliberately
     * SURVIVES, so a host that muted before a re-enumeration stays muted. */
    g_path_enabled = 0;
    codec_write_word();
}
