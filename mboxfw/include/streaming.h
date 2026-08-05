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

void streaming_set_rate(unsigned long hz);
void streaming_playback_enable(unsigned char on);
void streaming_capture_enable(unsigned char on);
void streaming_sof(void);

#endif
