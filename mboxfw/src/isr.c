/*
 * Interrupt service routines.
 *
 * hw_init.c enables EX0 (external INT0) and ET0 (Timer 0) via IE = 0x03,
 * and main() eventually sets EA = 1 to arm them. Without matching ISRs
 * SDCC-linked, IE + EA would vector into random code memory at addr
 * 0x0003 / 0x000B on the first interrupt — instant crash. These minimal
 * stubs claim those vectors and count firings so usb_service() /
 * buttons_poll() can consume the counters from the main loop.
 *
 * SDCC vector numbers:
 *   __interrupt(0) → INT0  (vector 0x03)
 *   __interrupt(1) → Timer 0 (vector 0x0B)
 *
 * Rev 20 uses INT0 to catch VECINT-side USB events and Timer 0 as a
 * millisecond tick (TH0 = 0xCE reload gives ~4 kHz overflow at 12 MHz
 * crystal / 12). We keep the same TH0/TL0 reload here — the counter is
 * primarily useful as a "did the CPU wedge?" liveness pulse until we
 * wire up proper SOF-driven work.
 */

#include "regs.h"

/* Bumped every time each ISR fires. Volatile so the main loop's reads
 * are not optimised out. Declared __data so they live in fast IRAM and
 * the ISR's read-modify-write inc doesn't touch xdata. */
volatile __data unsigned char g_int0_ticks   = 0;
volatile __data unsigned char g_timer0_ticks = 0;

/* INT0 (vector 0x03). Rev 20's handler (fcn.0x0DAC) reads VECINT to
 * dispatch USB events; for first flash we just acknowledge and count.
 * usb_service() polls VECINT independently, so we don't lose events. */
void isr_int0(void) __interrupt(0)
{
    g_int0_ticks++;
}

/* Timer 0 (vector 0x0B). Rev 20 reloads TH0 = 0xCE and sets a "pending"
 * flag (RAM[0x24].0). We do the same, minus the RAM flag which nothing
 * currently consumes. Reload is essential — Timer 0 is in mode 1
 * (16-bit, no auto-reload) so leaving TH0/TL0 unset would let the timer
 * roll all the way through 0x0000..0xFFFF between overflows instead of
 * the intended ~13k cycles. */
void isr_timer0(void) __interrupt(1)
{
    TH0 = 0xCE;
    TL0 = 0x00;
    g_timer0_ticks++;
}
