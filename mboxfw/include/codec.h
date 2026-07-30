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

/* Codec control-word bytes — externally visible so control handlers can poke
 * individual bits and then call codec_write_word(). */
extern __data unsigned char g_codec_state_23;
extern __data unsigned char g_codec_state_25;

#endif
