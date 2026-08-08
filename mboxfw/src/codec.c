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
/* #197 one-shot: the capture-gate pulse fires once per power-up. */
__data unsigned char g_adc_pulsed = 0;
/* SOF count when the master clocks came up, and whether it has been taken. */
__data unsigned char g_adc_clock_mark_set = 0;
__data unsigned int  g_adc_clock_mark = 0;

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

/* #197 diagnostic — see codec.h for what this is for and how it is read. */
__data unsigned char g_gate_probe = 0;

void codec_gate_probe(unsigned char mode)
{
    if (mode >= 2) { g_gate_probe = 1; return; }

    /* Deliberately the same two statements codec_apply_mute()'s class caller
     * uses, so that an ISR-context probe and the ALSA control differ in
     * nothing at all, and a main-loop probe differs only in context. */
    if (mode) { g_host_mute |= (unsigned char)CODEC23_MUTE_CAPTURE; }
    else      { g_host_mute &= (unsigned char)~CODEC23_MUTE_CAPTURE; }
    codec_apply_mute();
}

void codec_clear_adc_transient(void)
{
    /* #197. The capture gate must be driven HIGH, then held LOW, then raised
     * again. That low->high edge is what clears the ADC start-up transient,
     * and it clears it for the whole power-up. FINDING_197.
     *
     * BUILD 0x0039 GOT THIS WRONG AND WAS MEASURED WRONG ON HARDWARE. It
     * raised the pair, held it HIGH for ~860 ms, then lowered it -- the
     * opposite polarity, ending in the wrong state. After the cold boot that
     * flash required, both units still showed the transient in full: unit A
     * -33.4 dBFS in the first 100 ms with DC +0.127, unit B -36.8. The hold
     * duration was never the problem; the direction was.
     *
     * MINIMISED AGAINST HARDWARE, on units re-armed by a plain power cycle of
     * the 0x0039 image (whose pulse leaves the transient intact, which made it
     * a free fixture). A precise single-process pulse -- tools/mutepulse.c,
     * written because two amixer invocations cannot resolve below tens of ms --
     * cleared unit A with a 1 ms low-hold and unit B with NO usleep at all,
     * both going from about -38 dBFS in the first 100 ms to -101.
     *
     * So the requirement is the EDGE, not a settling time. The floor actually
     * proven is about 1 ms, because even a zero-usleep host pulse is still two
     * USB control transfers apart; two back-to-back codec_write_word() calls in
     * firmware would be far quicker than anything measured. One hw_short_delay()
     * is kept as the hold for that margin, and it costs less code than the
     * 8-iteration loop it replaces -- no counter, no loop.
     *
     * That hold is 78.2 ms, measured 2026-08-07, not the ~3 ms this comment
     * used to claim. Generous against a 1 ms requirement, and irrelevant next
     * to the 188.0 ms of digital zeros that RELEASING the gate costs -- also
     * measured that day, and the reason a pulse can never be free during a
     * live capture. FINDING_197.
     *
     * ENDS HIGH, deliberately, and that is also what makes it safe. usb_init()
     * runs first (task #47), so a SET_INTERFACE can land during the hold; a
     * sequence ending LOW would switch the capture path off underneath a
     * stream that had just opened, which is what 0x0039 risked. Ending HIGH is
     * correct either way, and it is what stock does anyway -- stock raises the
     * pair once at power-up (Rev 20 0x080B-0x0852, Rev 22 0x09B6-0x09F5) and
     * holds it for the session.
     *
     * NOVEL — reason: neither stock image pulses this gate. Stock raises the
     * pair once and never lowers it, and its per-stream path (Rev 20
     * 0x0395-0x03BD, Rev 22 0x0399-0x03C1) contains no mute and no long delay,
     * so the transient is original Mbox 1 behaviour and this is an improvement
     * over stock rather than a repair. FINDING_196 has the stock table. */
    /* EXACTLY the host path, not an equivalent-looking one.
     *
     * Builds 0x003D and 0x003E hand-wrote the same nominal bit sequence --
     * raise the pair, drop it, raise it, with codec_write_word() each time --
     * and both only PARTIALLY cleared the transient: -61 to -62 dBFS against
     * the -101 a host pulse reaches. The comparison that settles it was run on
     * one unit in one boot: after the firmware pulse the first 100 ms read
     * -60.9 dBFS, then a host pulse on the same unit read -101.2, and the codec
     * word was 0x1CC0 before and after both. Same end state, same bits, three
     * orders of magnitude apart in effect.
     *
     * Ruled out along the way: the delay is not elided (the generated asm keeps
     * both `lcall _hw_short_delay`), and dropping the whole pair rather than
     * just the capture gate changed nothing (0x003E vs 0x003D).
     *
     * So drive the mute the way a host does -- set g_host_mute, publish through
     * codec_apply_mute(), clear it, publish again. That path is measured to
     * work, repeatedly, on both units.
     *
     * BUILD 0x0040 MASKED INTERRUPTS ACROSS EACH PUBLISH, on the theory that
     * codec_write_word()'s bit-bang on P1 could be preempted mid-shift by the
     * USB ISR, stretching a clock phase, so that the codec mis-sampled a word
     * telemetry would still report as correct (block 9 mirrors what firmware
     * wrote, never what the shift register latched). It changed nothing, and
     * on 2026-08-07 the premise itself failed: the residual is the SAME
     * transient at 1/33 amplitude with tau = 171 ms intact, and a corrupted
     * word does not produce a correctly-shaped decay. The masking is dropped
     * -- it bought nothing, and its 18 bytes are what the gate probe needs.
     *
     * The body is now codec_gate_probe() twice, which is deliberate rather
     * than incidental: the diagnostic's main-loop arm then executes THIS
     * sequence exactly, so a clean gap in a recording is evidence about the
     * boot pulse itself and not about a lookalike. */
    codec_gate_probe(1);
    hw_short_delay();
    codec_gate_probe(0);
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
