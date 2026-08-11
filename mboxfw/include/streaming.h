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

/* #200 bench diagnostic. g_diag_clr_mask selects which pair bits
 * streaming_set_rate() clears before reprogramming: 0x0C shipping, 0x00
 * reproduces the pre-fix condition, 0x04/0x08 one bit each. Latched, defaulting
 * to the shipping value. g_diag_rst_cycles counts what actually happened.
 *
 * The structural arms (skip the reprogramming / skip the endpoint re-arm) did
 * NOT fit -- the image is 6008 of 6016 with this much -- so they are not here
 * rather than present and inert. A control that can be set and does nothing is
 * how four measurements were voided in one session.
 * See streaming.c and PLAN_200_reproduce_the_transient.md. */
extern __data unsigned char g_diag_clr_mask;
extern __data unsigned char g_diag_rst_cycles;

void streaming_set_rate(unsigned long hz);
void streaming_playback_enable(unsigned char on);
void streaming_capture_enable(unsigned char on);
void streaming_sof(void);
/* #186 stage 1: sample ACGCAP. Called on EVERY SOF, before the
 * playback-only watchdog, so the clock is measured even with no stream. */
void streaming_acg_sample(void);

#endif
