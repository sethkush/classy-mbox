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

#endif
