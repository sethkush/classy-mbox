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

/* Defensive RETI stubs for the four remaining 8051 interrupt vectors.
 * IE currently only enables EX0+ET0 (0x03), so nothing in this firmware
 * fires INT1/Timer1/UART/Timer2 — but SDCC only plants a vector-table
 * LJMP for vectors that have a matching __interrupt(N) declaration.
 * Without these stubs, the vector bytes at 0x0013/0x001B/0x0023/0x002B
 * are 0xFF gaps that fall through into the next code block; if any
 * future change unmasks one of these sources without also adding a
 * handler, the CPU vectors into random code and crashes.
 *
 * Both Rev 20 and Rev 22 defend against this — Rev 20 via LJMP-to-shared-
 * RETI-trampoline, Rev 22 via inlined RETI at each vector. safety_net
 * has these same 4 stubs at safety_net/src/main.c near the ISR block.
 * Fork audit 2026-07-24 flagged their absence in mboxfw. */
void isr_int1  (void) __interrupt(2) { }
void isr_timer1(void) __interrupt(3) { }
void isr_uart  (void) __interrupt(4) { }
void isr_timer2(void) __interrupt(5) { }
