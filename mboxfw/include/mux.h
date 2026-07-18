#ifndef MBOXFW_MUX_H
#define MBOXFW_MUX_H

extern __data unsigned char g_mux_state;    /* mirrors Rev 20 RAM[0x22] */
extern __bit g_phantom_48v;                 /* mirrors Rev 20 RAM[0x23].6 */

void mux_write(unsigned char state);

#endif
