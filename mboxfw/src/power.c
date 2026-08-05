/*
 * USB suspend / resume.
 *
 * mboxfw had no suspend path at all: USBIMSK unmasked SUSR and RESR, the
 * VECINT default case swallowed both, and PCON was never written anywhere in
 * the firmware. A bus-powered device that keeps its clock generators running
 * and its analog path live through suspend draws suspend current it is not
 * allowed to draw (USB 2.0 §7.2.3), and on this part it also leaves the codec
 * shift chain and panel LEDs asserted while the host believes the device is
 * asleep.
 *
 * Stock's design splits the work in two. The SUSR vector handler does nothing
 * but post a work code:
 *
 *   Rev 20 0x0006  MOV 0x0A,#0x0E ; RET      (VECINT slot 0x16)
 *   Rev 22 0x0006  identical
 *
 * and the main loop dispatches code 0x0E to the real sequence, with interrupts
 * enabled, because it blocks in PCON idle for as long as the host keeps the
 * bus quiet. RESR (VECINT slot 0x15) has no handler at all in either image
 * (Rev 20 table entry -> 0x1035, a bare RET; Rev 22 -> 0x102D): resume is not
 * an event to be handled, it is simply whatever interrupt wakes the CPU out of
 * idle, after which execution falls through to the re-init tail below.
 */

#include "regs.h"
#include "mux.h"
#include "codec.h"
#include "cs8427.h"   /* cs8427_boot_init() — #175 re-arm */
#include "power.h"
#include "telemetry.h"
#include "usb.h"

extern void hw_init(void);

/*
 * Port of Rev 20 0x0526-0x0563 (Rev 22 0x0525-0x0562), instruction by
 * instruction:
 *
 *   0526  MOV CY,0x0e ; ORL CY,0x0a ; JNC 0x0564   ; only if configured
 *   052c  ACGCTL &= 0x3F                           ; idle both clock gens
 *   0533  RAM[0x25] = 0 ; RAM[0x23] = 0 ; LCALL 0x0E62   ; codec word = 0
 *   053b  RAM[0x22] = 0xFF ; CLR 0x1E ; LCALL 0x0F0C     ; panel off, mono off
 *   0543  PCON |= 0x01                             ; ---- IDLE, CPU sleeps ---
 *   0546  USBCTL &= 0x7F                           ; drop CONN
 *   054d  USBIMSK = 0x9F
 *   0551  LCALL 0x08CB                             ; re-run hw_init
 *   0554  LCALL 0x0970                             ; re-run EP0 setup
 *   0557  SETB TR0 ; SETB EX0 ; SETB EA
 *   055d  USBCTL |= 0x80                           ; re-assert CONN
 *   0564  RAM[0x0A] = 0                            ; clear the work code
 */
static void do_suspend(void)
{
    /* The guard. Bits 0x0E and 0x0A are RAM[0x21].6 and RAM[0x21].2.
     * 0x21.6 is set by SET_CONFIGURATION for wValue 1 or 2 and cleared for 0
     * (Rev 20 0x0267 / 0x0279 / 0x0288), i.e. "configured". 0x21.2 is never
     * SET anywhere in either image — only cleared — so the OR reduces to the
     * single configured test, and an unconfigured device does not suspend.
     * See disasm/IRAM_BITS_ANNOTATION.md §"0x21.2 is never set anywhere". */
    if (!usb_is_configured()) {
        return;
    }

    /* Idle both adaptive clock generators. Clearing bits 7-6 of ACGCTL is the
     * inverse of the `ACGCTL |= 0xC0` that streaming.c does when a clock mode
     * is applied (Rev 20 0x07CC). */
    ACGCTL &= 0x3F;

    /* Codec word to all zeros. Every bit of it is active-high, which is what
     * makes 0x0000 the correct off-state and is itself part of the evidence
     * that the word is active-high: stock chooses this value for the one
     * moment it wants everything off. */
    g_codec_state_23 = 0;
    g_codec_state_25 = 0;
    codec_write_word();

    /* Panel word all-ones and mono cleared. 0xFF is the blanked state — the
     * source fields hold 0x07, which is not one of the three legal patterns,
     * so nothing is selected. This is the third of the three sites that write
     * this byte with an immediate. */
    g_mux_state = 0xFF;
    MONO_OFF();
    mux_write(g_mux_state);

    tlm_suspends++;

    /* ---- Sleep. Any enabled interrupt resumes execution on the next line.
     * PCON bit 0 is IDL; bit 1 (PD, power-down) is deliberately NOT set —
     * power-down stops the oscillator and the USB engine with it, so the
     * device could not see the resume signalling that has to wake it. Stock
     * sets bit 0 only. This is the only PCON write in the firmware. */
    PCON |= 0x01;

    /* ---- Resumed. Rebuild everything the sleep may have disturbed, in
     * stock's order. Dropping CONN first means the host sees a clean detach /
     * re-attach rather than a device that answers mid-reconfiguration.
     * Rev 20 fcn.0x0526 @ 0x0546 (Rev 22 fcn.0x0525 @ 0x0545). */
    USBCTL &= (unsigned char)~USBCTL_CONN;

    /* Deliberate divergence, TWO ways, both stated so neither is silent.
     *
     * Stock writes USBIMSK = 0x9F here (Rev 20 @ 0x054E, Rev 22 @ 0x054D,
     * both reached by `INC DPTR` from the USBCTL store at 0xFFFC). 0x9F is
     * RSTR|SOF|PSOF|SETUP|STPOW — it does NOT include SUSR (bit 6) or RESR
     * (bit 5). So stock, having handled one suspend, masks the source off and
     * never suspends again for the rest of that attach. Whether that is
     * intentional or an oversight is not decidable from the bytes; either way
     * copying it would make our suspend a one-shot, which defeats the point of
     * having one.
     *
     * We therefore restore the same mask usb_init() sets, keeping SUSR and
     * RESR live so the second and later suspends work too. And we OR rather
     * than assign, per task #48: USBIMSK is boot-ROM-owned, and a raw
     * assignment discards whatever the engine had set.
     *
     * Rev 20 fcn.0x0526 @ 0x054E is the site diverged from; the 0xF5 value is
     * the one usb_init() already uses. */
    USBIMSK |= 0xF5;

    hw_init();          /* Rev 20 fcn.0x0526 @ 0x0551 -> fcn.0x08CB */
    usb_ep0_setup();    /* Rev 20 fcn.0x0526 @ 0x0554 -> fcn.0x0970 */
    TR0 = 1;            /* Rev 20 fcn.0x0526 @ 0x0557 */
    EX0 = 1;            /* Rev 20 fcn.0x0526 @ 0x0559 */
    EA  = 1;            /* Rev 20 fcn.0x0526 @ 0x055B */
    /* Re-attach. Rev 20 fcn.0x0526 @ 0x055D (Rev 22 fcn.0x0525 @ 0x055C). */
    USBCTL |= USBCTL_CONN;
}

void work_dispatch(void)
{
    unsigned char code = g_work_code;

    switch (code) {
        case WORK_BRINGUP:
            /* #175. Guarded internally on IRAM 0x25.6, so this costs one test
             * and a return unless a suspend cleared the codec word. Stock's
             * equivalent is the `LCALL 0x080b` at the head of cmd2/cmd3. */
            cs8427_boot_init();
            break;
        case WORK_SUSPEND:
            do_suspend();
            break;
        default:
            break;
    }

    /* Rev 20 0x0564: CLR A; MOV 0x0A,A — every dispatched code lands on this
     * shared tail. Cleared AFTER the handler runs, so a code posted again by
     * an interrupt during the handler is not lost silently: it is re-posted
     * and this clear is the only thing that could drop it, which is why stock
     * clears rather than pre-clears. */
    g_work_code = WORK_NONE;
}
