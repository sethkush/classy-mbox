// MATCH: image=rev20 addr=0x0386 len=119 func=cmd2_apply_iface1_alt cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void audio_path_reconfig_ext_chips(void);
extern void shiftreg8_commit(void);
extern void shiftreg16_commit(void);
extern void dma0_disable(void);
extern void ep0_arm_zlp(void);
extern void evt_dispatch_epilogue(void);

/* Directions are pinned by usb_ep_dma_init's DMA channel programming, not by
 * guesswork: DMACTL0 = 0x02 (EPDIR=0, EPNUM=2) at rev20 0x09E0 / rev22 0x0901,
 * DMACTL1 = 0x09 (EPDIR=1, EPNUM=1) at rev20 0x09E6 / rev22 0x0907. EP1 IN is
 * device-to-host = capture; EP2 OUT is host-to-device = playback. */
SFRX(IEPCNF1, 0xFF60);   /* IN endpoint 1 config  — the capture stream  */
SFRX(OEPCNF2, 0xFF98);   /* OUT endpoint 2 config — the playback stream */

__bit __at (0x08) f_iface1_alt;   /* IRAM 0x21.0 — set by std_set_interface (0x029F) */
__bit __at (0x0A) f_cfg_alt;      /* IRAM 0x21.2 */
__bit __at (0x0E) f_configured;   /* IRAM 0x21.6 */
/* IRAM 0x22.7, i.e. bit 7 of g_mux_byte, the byte shiftreg8_commit clocks out
 * of P1.7/P1.6/P1.5. Cleared when the stream starts and set when it stops, and
 * touched nowhere else in either image (rev20 0x03A0 / 0x03E6, rev22 0x03A4 /
 * 0x03EA). What it drives on the panel is not established here. */
__bit __at (0x17) f_mux_bit7;
__bit __at (0x2E) f_ext_ready;    /* IRAM 0x25.6 — see cmd1_apply_clock_mode.c */

/* Event 2 — dispatched from event_jump_table entry 1 (rev20 0x0303 LJMP 0x0386,
 * rev22 0x030F LJMP 0x038A), queued by std_set_interface when the host writes
 * interface 1's alternate setting. Interface 1 is the audio stream, so this is
 * where the box actually starts and stops running audio.
 *
 * Both arms re-test `f_cfg_alt || f_configured`. That is the shape of
 *
 *     if (X && alt)  { start }
 *     else if (X && !alt) { stop }
 *
 * and not `if (X) { if (alt) ... else ... }`: the failure branch of the first
 * guard targets the *second guard* (rev20 0x0389 JNB 0x0E,0x03D6;
 * rev22 0x038D JNB 0x0E,0x03DA), not the
 * function tail, so the source really did evaluate X twice.
 *
 * Start path, in order:
 *   - one-shot external-chip bring-up, if it has not run since the last
 *     unconfigure;
 *   - front panel forced to a fixed state (g_mux_byte = 0xFF plus five bits
 *     cleared in the second shift chain) and both chains clocked out;
 *   - IEPCNF1 = 0xC5 -- the same constant audio_clock_mode_apply's common tail
 *     writes to
 *     IEPCNF1 and OEPCNF2 (rev20 0x07E4 and 0x07EA, rev22 0x07C5 and 0x07CB);
 *   - audio_clock_mode_apply(3) = 48000 Hz. Mode 3 is fixed here: the stream
 *     always comes up at 48 k regardless of what the host later asks for via
 *     SET_CUR. The mode number is passed in R7 (Keil register parameter), so
 *     the call is assembly;
 *   - DMACTL1.7 set to arm the EP1 IN (capture) DMA channel, and if the device
 *     is in the full configuration, OEPCNF2 and DMACTL0.7 for the EP2 OUT
 *     (playback) side as well.
 *
 * Stop path: DMACTL1.7 cleared, panel bit 7 raised, chain clocked, and
 * DMACTL0.7 cleared through dma0_disable when the playback side was running.
 *
 * The USBIMSK write is unconditional -- both arms and the do-nothing fall
 * through reach it (five branch sources converge on rev20 0x03F1, rev22
 * 0x03F5). It promotes the interrupt mask from the 0x9F that usb_ep_dma_init
 * left there (rev20 0x09EF, rev22 0x0910) to 0xFF. Per the datasheet's USBIMSK
 * layout -- 7 RSTR, 6 SUSR, 5 RESR, 4 SOF, 3 PSOF, 2 SETUP, 1 reserved,
 * 0 STPOW -- the two bits that turn on are 6 and 5, suspend and resume. The
 * device only asks to be told about suspend/resume once the host has begun
 * driving the interface-1 alternate setting. */
void cmd2_apply_iface1_alt(void) {
    if ((f_cfg_alt || f_configured) && f_iface1_alt) {
        if (!f_ext_ready) audio_path_reconfig_ext_chips();
        f_force = 0;
        g_mux_byte = 0xFF;
        pa_src0 = 0;
        pb_src0 = 0;
        p_hold = 0;
        f_mux_bit7 = 0;
        shiftreg8_commit();
        sa0 = 0; sb0 = 0; sa1 = 0; sb1 = 0;
        f_spdif = 0;
        shiftreg16_commit();
        IEPCNF1 = 0xC5;
        __asm
            .globl _audio_clock_mode_apply
            mov   r7,#0x03              ; clock mode 3 = 48000 Hz
            lcall _audio_clock_mode_apply
        __endasm;
        DMACTL1 |= 0x80;
        if (f_configured) {
            OEPCNF2 = 0xC5;
            DMACTL0 |= 0x80;
        }
    } else if ((f_cfg_alt || f_configured) && !f_iface1_alt) {
        DMACTL1 &= 0x7F;
        f_mux_bit7 = 1;
        shiftreg8_commit();
        if (f_configured) dma0_disable();   /* DMACTL0 &= 0x7F */
    }
    USBIMSK = 0xFF;          /* 0x9F + SUSR | RESR */
    ep0_arm_zlp();
    evt_dispatch_epilogue(); /* tail call: encodes as LJMP 0x0564 */
}
