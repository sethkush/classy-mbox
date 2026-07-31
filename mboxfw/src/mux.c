/*
 * Input-mux shift register (74HC595-style) driver.
 * Ports Rev 20's fcn.0x0F0C.
 *
 * Wire protocol (bit-banged on P1):
 *   P1.7 = data, P1.5 = clock (rising edge samples), P1.6 = latch pulse.
 *
 * One byte is shifted MSB-first per call. After the eight data bits, the MONO
 * flag (Rev 20 RAM[0x23].6) is presented on the DATA line and the latch is
 * raised — effectively a ninth bit, which is why the latch tail is asymmetric.
 * See the tail-decode comment below.
 */

#include "regs.h"
#include "mux.h"
#include "codec.h"

__data unsigned char g_mux_state = 0x00;


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

    /* Latch cycle. Rev 20 fcn.0x0F0C @ 0x0F32-0x0F3F (Rev 22 fcn.0x0EFC at
     * the matching offsets) branches on bit 0x1E = RAM[0x23].6 = mono:
     *
     *   0f32  JNB 0x1e,0x0f39
     *   0f35  ORL P1,#0xC0      ; DATA high AND LATCH high, then RET
     *   0f39  ANL P1,#0x7F      ; DATA low
     *   0f3c  ORL P1,#0x40      ; LATCH high
     *   0f3f  ANL P1,#0xBF      ; LATCH low
     *
     * Both branches drive DATA to the mono value and then raise LATCH, so the
     * shared action is "present a ninth bit and latch". The asymmetry is that
     * the mono-set branch returns with LATCH still high and the clear branch
     * drops it. Reproduced exactly rather than tidied, because which of the
     * two states the hardware latches on is unverified and the stock sequence
     * is the only evidence available. */
    if (MONO_IS_SET()) {
        P1 |= (P1_MUX_LATCH_MASK | P1_MUX_DATA_MASK);
    } else {
        P1 &= ~P1_MUX_DATA_MASK;
        P1 |= P1_MUX_LATCH_MASK;
        P1 &= ~P1_MUX_LATCH_MASK;
    }
}
