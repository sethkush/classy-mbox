/*
 * Mbox 1 class-compliant firmware — main entry.
 *
 * Runs on TAS1020A 8051 core, loaded from I²C EEPROM (24C64 at addr 0x50)
 * by the TAS1020A boot ROM. Presents a standard USB Audio Class 1 device
 * (2 ch × 24 bit × 44.1/48 kHz) so no vendor driver is needed.
 */

#include "regs.h"
#include "cs8427.h"
#include "codec.h"
#include "buttons.h"
#include "mux.h"
#include "eeprom.h"

extern void hw_init(void);
extern void usb_init(void);
extern void usb_attach(void);
extern void usb_service(void);

/*
 * Boot-time DFU escape hatch — mirrors Rev 20's "hold source-1 while
 * plugging in → DFU" behavior. Runs BEFORE hw_init so it works even
 * when USB is completely broken. If P3.3 is held low continuously for
 * ~50 ms after reset, invalidate the EEPROM signature and warm-reset.
 * Next boot lands in boot-ROM bulletproof DFU (0xFFFF:0xFFFE), no
 * hardware disassembly required.
 *
 * This is the SECOND independent software recovery path (the first is
 * mboxflash --enter-dfu → handle_digi_enter_dfu). This one gates on a
 * physical button rather than a USB class request, so it works even
 * when enumeration itself is dead.
 *
 * A glancing touch during plug-in should NOT trigger — the check
 * requires the button to stay low across the full 50 ms sample window.
 */
static void check_boot_dfu_button(void)
{
    unsigned int i;
    unsigned char held = 1;
    for (i = 0; i < 0x5000; i++) {
        if (P3 & P3_BTN_CH1_MASK) { held = 0; break; }
    }
    if (held) {
        if (eeprom_smoke_test()) {
            (void)eeprom_invalidate_signature();
        }
        /* Re-enter boot ROM immediately. Plain `ljmp 0` with SDW=1
         * restarts mboxfw (RAM at 0x0000) — the invalidated signature
         * would only take effect on next power cycle. See regs.h. */
        RESET_TO_BOOT_ROM();
    }
}

/*
 * ISR forward declarations. SDCC's vector-table emitter runs in the
 * compilation unit that owns crt0 (main.c here) and only lays down a
 * jump at 0x0003/0x000B/etc when it sees the matching __interrupt(N)
 * declaration in the same TU. The bodies live in isr.c — these
 * declarations exist purely to plant the vector jumps.
 * (Without them: the linker leaves the vector bytes as 0xFF/random, so
 *  the first INT0/Timer0 firing jumps into the middle of some function
 *  and crashes the CPU.)
 */
void isr_int0(void)    __interrupt(0);
void isr_timer0(void)  __interrupt(1);
/* Forward decls for the 4 defensive RETI stubs in isr.c. SDCC only
 * plants vector-table LJMPs for __interrupt(N) declarations visible in
 * the compilation unit that owns crt0 (this one) — a body in isr.c
 * alone is not enough; the vector at 0x0013/0x001B/0x0023/0x002B stays
 * a 0xFF gap. Both Rev 20 and Rev 22 defend against these vectors.
 * Fork audit 2026-07-24. */
void isr_int1  (void)  __interrupt(2);
void isr_timer1(void)  __interrupt(3);
void isr_uart  (void)  __interrupt(4);
void isr_timer2(void)  __interrupt(5);

/* Phase-completion canaries — written at each boundary so sim_smoke
 * (and any future runtime verifier that can peek shared RAM) can prove
 * exactly which init phases ran. Different values per phase means we
 * can distinguish "hung in cs8427" from "hung in codec" from "hung in
 * usb_init" without a scope. Canary window is 0xFA00..0xFA03 in the
 * TAS1020A shared-memory area — sits BELOW our EP0 buffers (0xFA10)
 * so it can't collide with USB packet memory. NOVEL — reason: new
 * addresses under 0xFA10 aren't used by TI's engUsbInit or Rev 20's
 * boot init; verified by grepping both. */
#define CANARY_BASE     0xFA00
#define CANARY(n, v)    (*(volatile __xdata unsigned char *)(CANARY_BASE + (n))) = (v)
#define CANARY_MAIN     0xA1   /* main() entered */
#define CANARY_USB      0xA2   /* usb_init returned */
#define CANARY_HW       0xA3   /* hw_init returned */
#define CANARY_CS8427   0xA4   /* cs8427_boot_init returned */
#define CANARY_CODEC    0xA5   /* codec_init returned */
#define CANARY_LOOP     0xA6   /* about to enter main polling loop */

/* --- LED progress canary (diagnostic build: make CANARY_LED=1) -------
 *
 * The XDATA canary above is only readable over USB, which is useless
 * when USB is the thing that is broken. This one reports through the
 * front panel instead. It is what located the EP0 desync in safety_net
 * in two cycles after weeks of guessing.
 *
 * Stage ladder, blinked as TENS long flashes then UNITS short ones:
 *
 *    1  main() entered            10  usb_isr fired at all
 *    2  usb_init returned         11  VECINT gave a real source
 *    3  hw_init returned          12  VEC_RSTR seen
 *    4  boot-DFU button checked   13  VEC_SETUP seen
 *    5  EA = 1                    14  GET_DESCRIPTOR(Device) served
 *    6  usb_attach returned       15  SET_ADDRESS received
 *    7  cs8427_boot_init returned 16  USBFADR written
 *    8  codec_init returned       17  SET_CONFIGURATION received
 *    9  main loop entered
 *
 * Output is the 8-bit panel latch (P1.7 data / P1.5 clk / P1.6 latch),
 * toggled between 0xFF (all six source LEDs off) and 0xF6 (the two mic
 * LEDs on) — the two bytes Rev 20's own hw_init writes, so the control
 * lines 0x22.6/.7 stay high throughout and no new hardware state is
 * created. See firmware_stock/disasm/PANEL_LEDS.md.
 *
 * 2026-07-27 baseline being investigated: mboxfw drives the panel to the
 * two-mic state and holds, which means hw_init completed, yet the device
 * is entirely absent from USB — not even a half-enumerated entry. Stages
 * 6 through 9 bracket whether usb_attach actually ran and whether the
 * audio init after it wedges. */
#ifdef CANARY_LED

#define PANEL_DARK 0xFF
#define PANEL_LIT  0xF6

volatile __data unsigned char g_stage = 0;
volatile __data unsigned char g_last_breq = 0xEE;  /* 0xEE = no SETUP yet */
volatile __data unsigned char g_stalls = 0;
#define STAGE(n) do { if ((unsigned char)(n) > g_stage) g_stage = (n); } while (0)

static void canary_delay(unsigned char units)
{
    unsigned char u;
    volatile unsigned int i;
    for (u = 0; u < units; u++)
        for (i = 0; i < 1500; i++) { }
}

/* Blink one value as TENS long flashes then UNITS short ones. */
static void canary_emit(unsigned char n)
{
    unsigned char k, tens = n / 10, units = n % 10;
    for (k = 0; k < tens; k++) {
        mux_write(PANEL_LIT);  canary_delay(24);
        mux_write(PANEL_DARK); canary_delay(10);
    }
    for (k = 0; k < units; k++) {
        mux_write(PANEL_LIT);  canary_delay(6);
        mux_write(PANEL_DARK); canary_delay(10);
    }
}

/* Report THREE numbers per cycle, separated by medium gaps, with a long
 * gap before the sequence repeats:
 *
 *   1st group — g_stage      how far the best case got
 *   2nd group — g_last_breq  bRequest of the most recent SETUP (decimal)
 *   3rd group — g_stalls     how many requests we answered with STALL
 *
 * The stage ladder alone is a monotonic maximum and cannot say WHICH
 * request is failing. g_last_breq answers that directly: 6 = the host is
 * still asking for descriptors, 5 = it never got past SET_ADDRESS, 9 =
 * it reached SET_CONFIGURATION. g_stalls says whether we are actively
 * rejecting things or simply never replying. */
static void canary_blink_forever(void)
{
    g_phantom_48v = 0;            /* never assert the unverified 0x23.6 */
    for (;;) {
        canary_emit(g_stage);
        canary_delay(40);
        canary_emit(g_last_breq);
        canary_delay(40);
        canary_emit(g_stalls);
        canary_delay(110);
    }
}

#else
#define STAGE(n) do { } while (0)
#endif /* CANARY_LED */

void main(void)
{
    /* DISCONNECT FIRST — defensive against boot-ROM-leftover USBCTL state.
     * Boot ROM's UsbDfu.c:699 zeroes USBCTL after DFU manifest, and cold
     * boot leaves USBCTL at hardware reset (0) — so on the expected paths
     * this is a no-op. Any deviation (e.g. boot ROM took a code path we
     * haven't audited that leaves USBCTL != 0) would race host enumeration
     * of boot ROM against our own attach. Rev 20 explicitly does this at
     * disasm 0x08E5 in its master init. Intentional assignment to
     * boot-ROM-owned SFR — POLICY §2 carve-out A. */
    USBCTL = 0;

    /* check_boot_dfu_button() runs after hw_init so P3 pull-ups are set
     * before the button is sampled, and before cs8427/codec init so the
     * escape hatch still works if either of those hangs. */

    CANARY(0, CANARY_MAIN);
    STAGE(1);

    /* usb_init() configures endpoints and buffers but does NOT attach.
     * Ordering below mirrors both stock firmwares exactly. */
    usb_init();
    CANARY(1, CANARY_USB);
    STAGE(2);

    hw_init();
    CANARY(2, CANARY_HW);
    STAGE(3);

    check_boot_dfu_button();
    STAGE(4);

    /* Interrupts on, then attach — the Rev 20 order (SETB EA at 0x0ACA,
     * USBCTL |= 0x80 at 0x0AD2, two instructions apart). */
    EA = 1;
    STAGE(5);
    usb_attach();
    STAGE(6);
    CANARY(5, CANARY_LOOP);

    /* Audio bring-up runs AFTER the attach. Rev 20 does it before, but
     * Rev 20's codec init is known-good; ours is not yet proven, and a
     * hang in either of these leaves the device unreachable if it happens
     * before we are on the bus. Because isr_int0 now services USB, the
     * host is answered throughout these two calls even if one wedges —
     * so a hang here costs audio, not the device.
     *
     * Move these back above the attach once both are hardware-proven. */
    cs8427_boot_init();
    CANARY(3, CANARY_CS8427);
    STAGE(7);

    codec_init();
    CANARY(4, CANARY_CODEC);
    STAGE(8);

    STAGE(9);
#ifdef CANARY_LED
    /* Diagnostic build: blink the stage instead of polling buttons.
     * buttons_poll() also drives the panel latch, so the two would
     * fight over it. USB is serviced from the ISR either way. */
    canary_blink_forever();
#endif

    for (;;) {
        /* USB is serviced from isr_int0 ONLY — see isr.c. Calling
         * usb_service() here as well would let the ISR re-enter it while
         * the loop is part-way through, and SDCC gives non-reentrant
         * functions static overlay locals, so the re-entry would corrupt
         * the EP0 transfer state. Rev 20 has the same division of
         * labour: its INT0 handler dispatches USB, its main loop handles
         * deferred panel/codec actions. */
        buttons_poll();
    }
}
