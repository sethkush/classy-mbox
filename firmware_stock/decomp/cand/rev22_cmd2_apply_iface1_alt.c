// MATCH: image=rev22 addr=0x038A len=115 func=cmd2_apply_iface1_alt cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void audio_hw_bringup(void);        /* rev22 0x09B6, rev20 0x080B */
extern void shiftreg_out8_p1hi(void);      /* rev22 0x0EFC, rev20 0x0F0C */
extern void shiftreg_out16_p1(void);       /* rev22 0x0E56, rev20 0x0E62 */
extern void dma0_disable(void);            /* rev22 0x0FF2, rev20 0x1001 */
extern void evt_arm_zlp_and_finish(void);  /* rev22 0x044E — shared event tail */

/* Endpoint directions are pinned by usb_ep_dma_init's DMA programming, not by
 * guesswork: DMACTL0 = 0x02 (EPDIR=0, EPNUM=2) at rev22 0x0901 / rev20 0x09E0,
 * DMACTL1 = 0x09 (EPDIR=1, EPNUM=1) at rev22 0x0907 / rev20 0x09E6. EP1 IN is
 * device-to-host = capture; EP2 OUT is host-to-device = playback. */
SFRX(IEPCNF1, 0xFF60);   /* IN endpoint 1 config  — the capture stream  */
SFRX(OEPCNF2, 0xFF98);   /* OUT endpoint 2 config — the playback stream */

__bit __at (0x08) f_iface1_alt;   /* IRAM 0x21.0 — set by std_set_interface   */
__bit __at (0x0A) f_cfg_alt;      /* IRAM 0x21.2 */
__bit __at (0x0E) f_configured;   /* IRAM 0x21.6 */
__bit __at (0x17) f_mux_bit7;     /* IRAM 0x22.7 = bit 7 of g_mux_byte        */
__bit __at (0x2E) f_ext_ready;    /* IRAM 0x25.6 — audio_hw_bringup run-once  */

/* ===================================================================== *
 * REV 20 -> REV 22 DELTA FOR THIS FUNCTION: NO BEHAVIOURAL CHANGE.
 *
 * rev20 0x0386..0x03FC (119 B) and rev22 0x038A..0x03FC (115 B) are
 * instruction-for-instruction identical for the first 113 bytes. Every opcode,
 * every bit address (0x0A, 0x0E, 0x08, 0x2E, 0x2D, 0x10, 0x13, 0x1E, 0x17,
 * 0x28..0x2C), every SFR address (0xFF60, 0xFFEE, 0xFF98, 0xFFE8, 0xFFFD) and
 * every immediate (0xFF, 0xC5, 0x80, 0x7F, R7=3) is the same; only the LCALL
 * operands moved with their callees. Measured directly:
 * rev20_firmware_code.bin[0x0386:0x03F7] vs
 * rev22_firmware_code.bin[0x038A:0x03FB] differ in exactly 10 bytes, and every
 * one is an operand byte of one of the six LCALLs:
 *
 *     callee                 rev20    rev22    rev20 site  rev22 site
 *     audio_hw_bringup       0x080B   0x09B6     0x0392      0x0396
 *     shiftreg_out8_p1hi     0x0F0C   0x0EFC     0x03A2      0x03A6
 *     shiftreg_out16_p1      0x0E62   0x0E56     0x03AF      0x03B3
 *     audio_clock_set_mode   0x0728   0x070F     0x03BA      0x03BE
 *     shiftreg_out8_p1hi     0x0F0C   0x0EFC     0x03E8      0x03EC
 *     dma0_disable           0x1001   0x0FF2     0x03EE      0x03F2
 *
 * (Ten and not twelve because 0x0E62/0x0E56 and 0x0728/0x070F each share a
 * high byte.)
 *
 * The whole 4-byte size difference is the epilogue, and it is a code-size
 * change only:
 *
 *     rev20 0x03F7  12 0F EA   LCALL ep0_arm_zlp
 *           0x03FA  02 05 64   LJMP  evt_dispatch_epilogue      (6 B)
 *     rev22 0x03FB  80 51      SJMP  0x044E                     (2 B)
 *
 * Rev 22 hoisted that pair into one shared copy at 0x044E which also inlines
 * the ZLP arming — see cand/rev22_evt_arm_zlp_and_finish.c. 119 - 4 = 115.
 *
 * So whatever Rev 22 fixed, it is NOT in the interface-1 SET_INTERFACE
 * handler. This is the function that starts and stops the capture stream, and
 * Rev 22 left it alone.
 * ===================================================================== */

/* USBIMSK CONFIRMATION (asked for explicitly).
 *
 * The 0x9F -> 0xFF promotion happens in THIS function, unconditionally, in
 * both images. Rev 22:
 *
 *     0x03F5  90 FF FD   MOV DPTR,#0xFFFD    ; USBIMSK
 *     0x03F8  74 FF      MOV A,#0xFF
 *     0x03FA  F0         MOVX @DPTR,A
 *
 * Rev 20 has the identical three instructions at 0x03F1 / 0x03F4 / 0x03F6.
 * The addresses quoted in the batch brief (rev20 0x03F4, rev22 0x03F8) are the
 * `MOV A,#0xFF`; the store lands at 0x03F6 / 0x03FA. Confirmed: same value,
 * same place in the control flow, four-byte relocation and nothing else.
 *
 * It is genuinely unconditional — five branch sources converge on the DPTR
 * load (rev22 0x03C8, 0x03D8, 0x03DD, 0x03E0, 0x03EF; rev20 0x03C4, 0x03D4,
 * 0x03D9, 0x03DC, 0x03EB), so the start arm, the stop arm and the
 * do-nothing fall-through all reach it.
 *
 * usb_ep_dma_init left USBIMSK at 0x9F (rev22 0x0910, rev20 0x09EF). Per the
 * TAS1020B USBIMSK layout — 7 RSTR, 6 SUSR, 5 RESR, 4 SOF, 3 PSOF, 2 SETUP,
 * 1 reserved, 0 STPOW — 0x9F -> 0xFF turns on bits 6 and 5, suspend and
 * resume. (Bit 4, SOF, is set in NEITHER value; see the project note that SOF
 * is masked off. Rev 22 did not change that either.) The device only asks to
 * be told about suspend/resume once the host has begun driving interface 1's
 * alternate setting. */

/* Event 2 — dispatched from event_jump_table entry 1 (rev22 0x030F LJMP
 * 0x038A, rev20 0x0303 LJMP 0x0386), queued by std_set_interface when the host
 * writes interface 1's alternate setting. Interface 1 is the capture stream,
 * so this is where the box actually starts and stops running audio.
 *
 * Both arms re-test `f_cfg_alt || f_configured`. That is the shape of
 *
 *     if (X && alt)       { start }
 *     else if (X && !alt) { stop  }
 *
 * and not `if (X) { if (alt) ... else ... }`: the failure branch of the first
 * guard targets the SECOND GUARD (rev22 0x038D JNB 0x0E,0x03DA; rev20 0x0389
 * JNB 0x0E,0x03D6), not the function tail, so the source really did evaluate
 * the guard twice.
 *
 * Start path, in order:
 *   - one-shot external-chip bring-up if it has not run since the last
 *     unconfigure (f_ext_ready, IRAM 0x25.6);
 *   - front panel forced to a fixed state (g_mux_byte = 0xFF plus five bits
 *     cleared in the second shift chain) and both chains clocked out;
 *   - IEPCNF1 = 0xC5 — the same constant audio_clock_set_mode's common tail
 *     writes to IEPCNF1 and OEPCNF2 (rev22 0x07C5 / 0x07CB, rev20 0x07E4 /
 *     0x07EA): endpoint enable | ISO | buffer config;
 *   - audio_clock_set_mode(3) = 48000 Hz. Mode 3 is hard-coded here: the
 *     stream always comes up at 48 k regardless of what the host later asks
 *     for via SET_CUR. The mode number is passed in R7 (Keil register
 *     parameter), so that call is written as assembly;
 *   - DMACTL1.7 (DMAEN) set to arm the EP1 IN capture channel, and if the
 *     device is in the full configuration, OEPCNF2 and DMACTL0.7 for the EP2
 *     OUT playback side as well.
 *
 * Stop path: DMACTL1.7 cleared, panel bit 7 raised, chain clocked out, and
 * DMACTL0.7 cleared through dma0_disable when the playback side was running.
 *
 * __naked because the exit is a two-byte SJMP into the shared tail at 0x044E:
 * the dispatcher's `break`, not a RET, and SDCC will not short-jump to an
 * external symbol. Everything before it is ordinary C. */
void cmd2_apply_iface1_alt(void) __naked {
    if ((f_cfg_alt || f_configured) && f_iface1_alt) {
        if (!f_ext_ready) audio_hw_bringup();
        f_force = 0;
        g_mux_byte = 0xFF;
        pa_src0 = 0;
        pb_src0 = 0;
        p_hold = 0;
        f_mux_bit7 = 0;
        shiftreg_out8_p1hi();
        sa0 = 0; sb0 = 0; sa1 = 0; sb1 = 0;
        f_spdif = 0;
        shiftreg_out16_p1();
        IEPCNF1 = 0xC5;
        __asm
            .globl _audio_clock_set_mode
            mov   r7,#0x03              ; clock mode 3 = 48000 Hz
            lcall _audio_clock_set_mode
        __endasm;
        DMACTL1 |= 0x80;                /* arm EP1 IN (capture) DMA */
        if (f_configured) {
            OEPCNF2 = 0xC5;
            DMACTL0 |= 0x80;            /* arm EP2 OUT (playback) DMA */
        }
    } else if ((f_cfg_alt || f_configured) && !f_iface1_alt) {
        DMACTL1 &= 0x7F;                /* stop the capture DMA */
        f_mux_bit7 = 1;
        shiftreg_out8_p1hi();
        if (f_configured) dma0_disable();   /* DMACTL0 &= 0x7F */
    }
    USBIMSK = 0xFF;                     /* 0x9F | SUSR | RESR */
    __asm
        /* `sjmp _evt_arm_zlp_and_finish` is what this is, but sdas cannot
         * encode a short jump to an external symbol -- it emits 80 00 and
         * leaves the displacement to the linker, which is one byte short of a
         * match (sdld does resolve it correctly; match51 has no rule to excuse
         * a relative operand). Written self-relative instead, the same way
         * cand/cmd12_set_cpt_mode1.c and cand/cmd11_eeprom_selftest.c do it:
         * `.` is this instruction's area-relative address, so the subtraction
         * is the constant 0x53 at assembly time and survives relocation
         * unchanged. Standalone and linked it is 80 51, i.e.
         * 0x03FB + 2 + 0x51 = 0x044E. */
        sjmp  . + (0x044E - 0x03FB)     ; -> evt_arm_zlp_and_finish
    __endasm;
}
