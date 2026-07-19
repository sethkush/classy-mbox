#ifndef MBOX_CODEC_H
#define MBOX_CODEC_H

/*
 * Codec / audio-path state initialiser.
 *
 * Ports Rev 20 fcn.0x0970 verbatim. Clears the state bytes RAM[0x22],
 * RAM[0x23], RAM[0x25] that drive the 74HC595 input-mux + CS4272-like
 * codec serial bus, kicks the mux to push the cleared state, then
 * propagates zeros to the codec state machine at fcn.0x0E62 (bit
 * manipulator on RAM[0x22]/[0x25]).
 *
 * Must be called after hw_init() and before usb_init() — Rev 20's flow.
 */
void codec_init(void);

/*
 * codec_commit() — run the state adjuster (fcn.0x0E62) then shift the
 * updated {high, low} codec bytes out to the codec via P1.0/P1.2 with
 * a P1.1 latch strobe. Ports fcn.0x0E74. Call after every state change
 * (mux update, phantom-power toggle, source cycle) to make the change
 * visible on the audio codec chip.
 *
 * The two bytes shifted are:
 *   high byte = g_codec_state_23 (mirrors Rev 20 RAM[0x23], control-word MSB)
 *   low byte  = g_codec_state_25 (mirrors Rev 20 RAM[0x25], control-word LSB)
 *
 * MSB-first, so bit 23.7 goes out first, bit 25.0 last.
 */
void codec_commit(void);

/* codec state bytes — externally visible so button/control handlers can
 * poke the individual bits before calling codec_commit(). */
extern __data unsigned char g_codec_state_23;
extern __data unsigned char g_codec_state_25;

#endif
