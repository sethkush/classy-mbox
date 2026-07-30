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

extern void usb_service(void);

/* Bumped every time each ISR fires. Volatile so the main loop's reads
 * are not optimised out. Declared __data so they live in fast IRAM and
 * the ISR's read-modify-write inc doesn't touch xdata. */
volatile __data unsigned char g_int0_ticks   = 0;
volatile __data unsigned char g_timer0_ticks = 0;

/* Timer-0 tick PENDING flag — stock's RAM[0x24].0, bit address 0x20.
 *
 * Distinct from g_timer0_ticks, which is a free-running count kept only as a
 * liveness pulse. This one is the handshake the main loop consumes: the ISR
 * sets it, the loop's panel pass runs once per set, and the loop clears it.
 * Rev 20 SETB 0x20 @ 0x1020 / test @ 0x0AD3 / CLR @ 0x0B0D;
 * Rev 22 SETB @ 0x1018 / test @ 0x0A7D / CLR @ 0x0AB7. */
volatile __bit g_timer0_pending = 0;

/* Deferred work code — stock's RAM[0x0A].
 *
 * Stock's USB event handlers do almost nothing in interrupt context: they
 * store a small code here and return, and the main loop dispatches it through
 * the jump table at Rev 20 0x0300 (index = code - 1). The suspend handler is
 * the clearest case — Rev 20's SUSR vector (VECINT slot 0x16) is the two
 * instructions at 0x0006, `MOV 0x0A,#0x0E; RET`, and every slow thing suspend
 * does (idling the clock generators, muting, entering PCON idle) happens later
 * at 0x0526 with interrupts on. Rev 22 is identical at 0x0006.
 *
 * 0 = nothing pending. */
volatile __data unsigned char g_work_code = 0;

/* INT0 (vector 0x03). Rev 20's handler (fcn.0x0DAC) reads VECINT to
 * dispatch USB events; for first flash we just acknowledge and count.
 * usb_service() polls VECINT independently, so we don't lose events. */
void isr_int0(void) __interrupt(0)
{
    g_int0_ticks++;
    /* Service USB from the ISR, as Rev 20 does (its INT0 handler at
     * 0x0DAC dispatches on VECINT rather than merely counting).
     *
     * Without this, USB is serviced ONLY from usb_service() in main()'s
     * loop, so any hang in the audio bring-up leaves the device attached
     * but permanently mute on the bus — indistinguishable from a dead
     * device, and unrecoverable without opening the case. With it, the
     * device answers the host even while cs8427_boot_init or codec_init
     * is stuck, which is what makes --enter-dfu a real recovery path
     * rather than a theoretical one. */
    usb_service();
}

/* Timer 0 (vector 0x0B). Stock's whole handler, Rev 20 @ 0x101E and Rev 22
 * @ 0x1016:
 *
 *   101e  CLR EA
 *   1020  SETB 0x20          ; RAM[0x24].0 — tick pending
 *   1022  MOV TH0,#0xCE
 *   1025  SETB EA
 *   1027  RETI
 *
 * The pending flag is what the main loop gates its panel pass on. It used to
 * be missing here — the comment said "minus the RAM flag which nothing
 * currently consumes", and nothing did, because main() called buttons_poll()
 * unconditionally on every pass of a loop that spins as fast as the CPU can
 * go. That polls P3 at hundreds of kHz and re-publishes the panel shift chain
 * on every contact bounce; stock polls it once per timer tick.
 *
 * Reload is essential — Timer 0 is in mode 1 (16-bit, no auto-reload) so
 * leaving TH0 unset would let the timer roll all the way through
 * 0x0000..0xFFFF between overflows instead of the intended ~13k cycles.
 *
 * Deliberate divergence: stock reloads TH0 only, leaving TL0 to continue from
 * wherever it stood, which makes the period jitter by up to 256 cycles. We
 * reload both for a stable tick. Nothing downstream measures absolute time, so
 * this is a free improvement rather than a parity break. */
void isr_timer0(void) __interrupt(1)
{
    TH0 = 0xCE;
    TL0 = 0x00;
    g_timer0_pending = 1;
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
