#ifndef MBOXFW_STREAMING_H
#define MBOXFW_STREAMING_H

/* The clock mode last applied, in STOCK'S numbering so the two can be compared
 * directly: 1 = slaved to S/PDIF (ACGCTL 0x0D, MCLKI), 2 = internal 44.1 kHz,
 * 3 = internal 48 kHz. This is Rev 20's RAM[0x08], written at 0x0753 (#1),
 * 0x0785 (#2) and 0x0791 (#3); Rev 22 at 0x0741 / 0x0773 / 0x077F.
 *
 * Stock reads it back in two places — `setup_get_sample_freq` @0x008A reports
 * 0,0,0 when it is 1, and cmd4 reloads it on a return to analog. mboxfw uses it
 * only for telemetry block 9; the reported rate lives in usb.c, and the
 * analog-restore path deliberately does NOT use this (see g_internal_rate). */
extern __data unsigned char g_clock_mode;

/* #200's runtime diagnostics are retired; see streaming.c. */

/* #201. g_ref_settled says the analog reference has had time to charge; the main
 * loop sets it. g_cal_done says a calibration has been taken since then, after
 * which no stream open pays the 183 ms. See streaming.c. */
extern __bit g_ref_settled;
extern __bit g_cal_done;

void streaming_set_rate(unsigned long hz);
void streaming_playback_enable(unsigned char on);
void streaming_capture_enable(unsigned char on);
void streaming_sof(void);
/* #186 stage 1: sample ACGCAP. Called on EVERY SOF, before the
 * playback-only watchdog, so the clock is measured even with no stream. */
void streaming_acg_sample(void);

/* #215/#211 bench knob: re-arm the feedback endpoint with a different byte
 * count, so the count can be swept without one flash per value. */
void streaming_set_feedback_count(unsigned char n);

#endif
