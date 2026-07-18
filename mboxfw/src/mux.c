/*
 * Input-mux shift register (74HC595-style) driver.
 * Ports Rev 20's fcn.0x0F0C.
 *
 * Wire protocol (bit-banged on P1):
 *   P1.7 = data, P1.5 = clock (rising edge samples), P1.6 = latch pulse.
 *
 * One byte is shifted MSB-first per call. If g_phantom_48v (mirror of Rev 20
 * RAM[0x23].6) is set, the latch is held high on both edges (asserts 48V);
 * otherwise a normal single-pulse latch.
 */

#include "regs.h"
#include "mux.h"

__data unsigned char g_mux_state = 0x00;
__bit g_phantom_48v = 0;

void mux_write(unsigned char state)
{
    unsigned char i;
    unsigned char v = state;

    /* Start: latch low (per Rev 20 anl 0x90, #0xbf at 0x0F10). */
    P1 &= ~P1_MUX_LATCH_MASK;

    for (i = 0; i < 8; i++) {
        if (v & 0x80) {
            P1 |= P1_MUX_DATA_MASK;
        } else {
            P1 &= ~P1_MUX_DATA_MASK;
        }
        /* Pulse clock. */
        P1 |= P1_MUX_SCLK_MASK;
        P1 &= ~P1_MUX_SCLK_MASK;
        v <<= 1;
    }

    /* Latch cycle. Rev 20 has two distinct patterns based on 0x23.6:
     *   - bit set: leave both LATCH and DATA high (P1 |= 0xC0)
     *   - bit clear: DATA low, LATCH pulse (0x40 → 0x00)
     */
    if (g_phantom_48v) {
        P1 |= (P1_MUX_LATCH_MASK | P1_MUX_DATA_MASK);
    } else {
        P1 &= ~P1_MUX_DATA_MASK;
        P1 |= P1_MUX_LATCH_MASK;
        P1 &= ~P1_MUX_LATCH_MASK;
    }
}
