#ifndef MBOX_CODEC_H
#define MBOX_CODEC_H

/*
 * Codec control-word state and the two routines that act on it.
 *
 * Naming correction, 2026-07-29. This header used to describe fcn.0x0E62 as a
 * "state adjuster (bit manipulator on RAM[0x22]/[0x25])" and fcn.0x0E74 as the
 * 16-bit serial write, with a note that 0x0E74 has zero callers and is
 * therefore dead code. Both halves were wrong, and they were wrong together:
 *
 *   Rev 20 0x0E62  MOV R6,#0x8       ; XREFs: 0x0AE9 0x096C 0x037D 0x0818
 *          0x0E64  MOV R5,0x23       ;        0x0835 0x0842 0x084D 0x0852
 *          0x0E66  SETB 0x30         ; RAM[0x26].0 = "on the high byte"
 *          ... 8x { present ACC bit 7 on P1.0; pulse P1.2 } ...
 *          0x0E8B  JNB 0x30,0x0E96   ; high byte done?
 *          0x0E8E  CLR 0x30 ; MOV R5,0x25 ; MOV R6,#8 ; loop again
 *          0x0E96  ORL P1,#0x02 ; ANL P1,#0xFD   ; latch pulse
 *          0x0E9C  RET
 *
 * 0x0E62 IS the 16-bit shift, it has eight callers, and 0x0E74 is not a
 * function at all — it is the `DJNZ R0` two instructions into 0x0E62's inner
 * rotate. Rev 22 has the same routine at 0x0E56.
 *
 * The bit-fiddling that was attributed to 0x0E62 is real but lives elsewhere:
 * it is the shared tail of the two source-cycle handlers (Rev 20 0x0E52-0x0E61
 * for channel 1 and 0x0EC5-0x0ED4 for channel 2; Rev 22 0x0E46-0x0E55 and
 * 0x0EB7-0x0EC6), reached only from those two handlers and running only when a
 * source button has been pressed — not on every publish.
 */

/*
 * Port of Rev 20 fcn.0x0E62 / Rev 22 fcn.0x0E56 — shift the 16-bit codec
 * control word out on P1.0 (data) clocked by P1.2, high byte first, MSB first,
 * then pulse P1.1 to latch.
 *
 *   high byte = g_codec_state_23 (RAM[0x23]) — so bit 23.7 goes out first
 *   low  byte = g_codec_state_25 (RAM[0x25]) — bit 25.0 goes out last
 *
 * Call after changing any bit of the word. Every bit in it is active-high;
 * suspend publishes 0x0000 to turn everything off.
 */
void codec_write_word(void);

/*
 * Port of the source-cycle tail (Rev 20 0x0E52-0x0E61 / 0x0EC5-0x0ED4).
 * Recomputes RAM[0x22].6 from RAM[0x25].4 and RAM[0x25].5:
 *
 *   0e52  JB  0x2C,0x0E57   ; 0x2C = RAM[0x25].4
 *   0e55  SETB 0x16         ; 0x22.6 = 1
 *   0e57  JNB 0x2C,0x0E5C
 *   0e5a  CLR 0x16          ; 0x22.6 = 0
 *   0e5c  JNB 0x2D,0x0E61   ; 0x2D = RAM[0x25].5
 *   0e5f  CLR 0x16          ; 0x22.6 = 0
 *
 * i.e. 0x22.6 = !(0x25.4) && !(0x25.5). Bit 0x22.6 is derived, never stored
 * independently. Call after cycle_source(), before publishing the mux word.
 */
void codec_source_changed(void);

/*
 * Zero the codec word and publish it. Ports the tail of stock's master hw
 * init: Rev 20 fcn.0x08CB @ 0x0967-0x096C (CLR A; MOV 0x25,A; MOV 0x23,A;
 * LCALL 0x0E62), Rev 22 @ 0x0888-0x088D.
 */
void codec_init(void);

/*
 * Named bits of the 16-bit codec control word.
 *
 * Every one of these is a physical control line on the second P1 shift chain.
 * They are named here because mboxfw drove exactly two of them until
 * 2026-07-31 — `tools/latch_word_bit_diff.py` reported g_codec_state_23 with a
 * settable mask of 0x0C and g_codec_state_25 with a mask of 0x00, i.e. the
 * whole low byte was write-zero-only. See
 * FINDING_codec_word_is_two_bits_of_sixteen.md.
 *
 * Bit assignments come from the setter sites in both stock images; the gate
 * pins each one and fails on an unexplained gap.
 */
/* #189 SETTLED 2026-08-06: these are TWO gates, not one, and they separate the
 * directions. 0x23.2 gates CAPTURE and 0x23.3 gates PLAYBACK -- each kills
 * exactly one arm and leaves the other untouched, on both units, bracketed.
 * Muting capture yields exact digital zeros (0 of 240000 samples); muting
 * playback leaves the far unit's own -100 dBFS input floor, which a single
 * global gate could not produce. FINDING_189_the_mute_pair_separates.md.
 *
 * #171 read them as ONE global enable because its build removed both bits at
 * once -- the correct reading of that experiment, which could not tell one gate
 * from two. */
#define CODEC23_MODE5_A      0x04u  /* 0x23.2 — CAPTURE  path enable (#189) */
#define CODEC23_MODE5_B      0x08u  /* 0x23.3 — PLAYBACK path enable (#189) */
#define CODEC23_MUTE_CAPTURE   CODEC23_MODE5_A
#define CODEC23_MUTE_PLAYBACK  CODEC23_MODE5_B
/* #46. The pair as one mask, so the mute-split experiment
 * (MBOX_MUTE_PAIR_MASK in the Makefile) can vary it without putting an
 * unresolvable macro in front of latch_word_bit_diff.py -- that gate reads
 * the CODEC23_ and CODEC25_ defines out of this file and over-estimates anything it
 * cannot resolve, which is safe but reports a false stale-gap. */
#ifndef CODEC23_MUTE_PAIR
#define CODEC23_MUTE_PAIR    0x0Cu  /* CODEC23_MODE5_A | CODEC23_MODE5_B */
#endif
/* Both pair bits regardless of what the boot mask above was built as. #189's
 * runtime control writes this field, and it must stay the full pair even in a
 * variant build -- otherwise the request could not restore a bit the boot mask
 * left low, which is the one thing it exists to do. */
/* Spelled as a LITERAL, not as `CODEC23_MODE5_A | CODEC23_MODE5_B`, for the
 * same reason the note above gives for CODEC23_MUTE_PAIR: latch_word_bit_diff.py
 * resolves the CODEC23_ defines out of this file and cannot expand one whose
 * value is itself an expression over other macros. A compound definition here
 * makes every store using it read as "can set all eight bits", which turns the
 * documented 0x23.0/0x23.1 gaps into apparent drivers and fails the gate. */
#define CODEC23_MUTE_PAIR_ALL  0x0Cu  /* = CODEC23_MODE5_A | CODEC23_MODE5_B */
#define CODEC23_RESET_N      0x10u  /* 0x23.4 — external-chip RESET, ACTIVE LOW.
                                     * Rev 20 SETB 0x1c @0x0840 (Rev 22 @0x09E5,
                                     * @0x0BB5) releases it once at boot. */
#define CODEC23_MONO         0x40u  /* 0x23.6 — mono. Rev 20 @0x0941, @0x102E. */

#define CODEC25_SRC1_LO      0x01u  /* 0x25.0 \ channel-1 source state, 2 bits  */
#define CODEC25_SRC2_LO      0x02u  /* 0x25.1 \ channel-2 source state          */
#define CODEC25_SRC1_HI      0x04u  /* 0x25.2 / see cycle_source() for the map  */
#define CODEC25_SRC2_HI      0x08u  /* 0x25.3 /                                 */
#define CODEC25_SEL_SPDIF    0x10u  /* 0x25.4 — UAC Selector Unit position      */
#define CODEC25_SPDIF_RX     0x20u  /* 0x25.5 — S/PDIF receiver engaged         */
#define CODEC25_BRINGUP_DONE 0x40u  /* 0x25.6 — bring-up-has-run guard.
                                     * Rev 20 SETB 0x2e @0x0810. */
#define CODEC25_CS8427_CS_N  0x80u  /* 0x25.7 — CS8427 chip select, ACTIVE LOW.
                                     * Rev 20 CLR/SETB 0x2f @0x0C4F/@0x0C8D. */

/*
 * Mono lives in the codec word, bit 0x23.6, exactly as it does on stock —
 * `SETB 0x1e` at Rev 20 0x0941 and 0x102E, and `mux_write`'s ninth-bit tail
 * reads that same bit (`JNB 0x1e` at Rev 20 0x0F32).
 *
 * mboxfw used to keep it in a separate `__bit g_mono`, which meant the panel
 * chain got the right value and the CODEC word's bit 6 was always 0. Making
 * the word the single source of truth removes the chance of the two drifting,
 * which a mirror-at-each-assignment fix would have left open.
 */
#define MONO_ON()     (g_codec_state_23 |= CODEC23_MONO)
#define MONO_OFF()    (g_codec_state_23 &= (unsigned char)~CODEC23_MONO)
/* Only for a genuinely dynamic value — SDCC warns 126 (unreachable code) if
 * this is handed a compile-time constant, so the constant sites use ON/OFF. */
#define MONO_SET(v)   do { if (v) { MONO_ON(); } else { MONO_OFF(); } } while (0)
#define MONO_IS_SET() ((g_codec_state_23 & CODEC23_MONO) != 0)

/* #190. Which paths the HOST has muted, in CODEC23_ bit positions -- so
 * CODEC23_MUTE_CAPTURE set here means capture is muted.
 *
 * This exists because the pair cannot be used as its own state. Every stream
 * open runs streaming_set_rate(), which raises the pair and publishes; a host
 * that muted a stream and then reopened it -- SET_INTERFACE plus the
 * sampling-frequency SET_CUR, entirely ordinary -- would find the mute
 * silently cleared. #189 hit exactly this as a test artifact before it was a
 * bug: the first working run of test_mute_pair.sh set the mask and then
 * started the streams, which undid it before a sample was captured.
 *
 * So set_rate raises `CODEC23_MUTE_PAIR & ~g_host_mute` rather than the whole
 * mask, and this variable is the one place the answer lives. */
extern __data unsigned char g_host_mute;

/* Which paths streaming_set_rate() has ENABLED, in the same bit positions.
 * Separate from g_host_mute because the pair alone cannot distinguish a path
 * that is down from one that is muted, and unmute needs to know the
 * difference. */
extern __data unsigned char g_path_enabled;

/* Recompute the pair from g_path_enabled and g_host_mute, and publish. This is
 * the ONLY writer of those two bits outside streaming_set_rate(), which sets
 * g_path_enabled and folds the same expression in-line so that it can publish
 * at stock's point in the sequence rather than early. */
void codec_apply_mute(void);

/* #197. One pulse of the capture gate at boot, which clears the ADC start-up
 * transient for the rest of the power-up. Call once, after the codec and the
 * CS8427 are up. FINDING_197. */
void codec_clear_adc_transient(void);
extern __data unsigned char g_adc_pulsed;
extern __data unsigned char g_adc_clock_mark_set;
extern __data unsigned int  g_adc_clock_mark;
/* SOFs to wait after the clocks come up before pulsing. 2500 = 2.5 s, which
 * is the shortest clocks-on time actually measured to work; the failures at
 * ~0 ms bracket it from below and nothing narrows it further. */
#define ADC_PULSE_DELAY_SOF  2500u

/* Codec control-word bytes — externally visible so control handlers can poke
 * individual bits and then call codec_write_word(). */
extern __data unsigned char g_codec_state_23;
extern __data unsigned char g_codec_state_25;

#endif
