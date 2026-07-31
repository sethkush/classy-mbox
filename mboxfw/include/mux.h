#ifndef MBOXFW_MUX_H
#define MBOXFW_MUX_H

extern __data unsigned char g_mux_state;    /* mirrors Rev 20 RAM[0x22] */

/* Mirrors Rev 20 RAM[0x23].6 (bit address 0x1E) — the MONO fold-down flag.
 *
 * This was called g_phantom_48v for most of the project's life. That was
 * wrong twice over. 48V phantom power on the Mbox 1 is a mechanical latching
 * switch with no firmware bit at all (confirmed on hardware: flipping it lit
 * its own LED with no firmware running that could have done it). And 0x23.6
 * is provably the mono flag: the only routine that toggles it is the handler
 * the P3.5 button calls, Rev 20 fcn.0x1028 / Rev 22 fcn.0x1020, which is a
 * bare complement (JNB 0x1E → SETB / else CLR) with no other effect — a
 * toggle, matching a momentary button, where 48V is a switch. Boot clears it
 * (Rev 20 0x095E-0x0962 @ 0x0962, Rev 22 0x087F-0x0883 @ 0x0883) and the
 * panel comes up with mono off, as observed. */


void mux_write(unsigned char state);

#endif
