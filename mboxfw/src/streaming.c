/*
 * Audio streaming — EP1 IN (capture) + EP2 OUT (playback).
 *
 * The TAS1020A moves bytes between USB packet memory and the C-port (I²S)
 * via two DMA channels. Our job is:
 *   1. Configure the DMA source/dest pointers for the current sample rate.
 *   2. Configure IEPCNF1 / OEPCNF2 to enable the streaming endpoints.
 *   3. Handle SET_INTERFACE(alt=1) to start streaming and alt=0 to stop.
 *   4. On per-frame SOF interrupt, hand off freshly-filled or newly-drained
 *      packet-memory buffers between USB and the DMA channels.
 *
 * The 6-byte DMA constants for 48 kHz come straight from Rev 20's fcn.0x0DEC.
 * For 44.1 kHz the values would differ — Rev 20 branches to distinct code
 * paths for each rate; we'll fill those in from the same table once we
 * capture them (or empirically, by observing what Digi wrote for each mode).
 */

#include "regs.h"
#include "usb.h"
#include "streaming.h"
#include "codec.h"

/* Currently-active sample rate — mirrors g_sample_rate in usb.c. */
static __data unsigned long stream_rate = 48000UL;

/*
 * DMA source constants decoded from Rev 20's fcn.0x0728 mode-dispatch
 * (rev20_flat.asm around 0x074d..0x079c) plus its 48 kHz tail at
 * 0x0DFE..0x0E28. Two distinct value sets are written to DMASRC0 and
 * DMASRC2 depending on which mode the clock-source switcher enters:
 *
 *   mode 3 tail (fcn.0x0DEC / 0x0DFE): DMASRC = 0x0F_A861
 *   mode 2       (fcn.0x0728 @ 0x075f): DMASRC = 0x20_4B6A
 *
 * The mapping of Rev 20's internal "mode" number → sample rate is:
 *   RAM[0x0A] = 7  →  44.1 kHz internal (see NOTES.md class-SET handler)
 *   RAM[0x0A] = 8  →  48   kHz internal
 * The full dispatch chain from RAM[0x0A] → fcn.0x0728 is deep enough that
 * we're taking it on the balance of evidence rather than fully traced:
 * the 48 kHz path (mode 3 tail) has been sanity-checked against my
 * earlier RE, so 44.1 kHz is the other one (mode 2's 0x20_4B6A). If audio
 * comes out pitched WRONG at 44.1 on first flash, swap the two calls
 * below — that's the fastest way to falsify the assumption.
 */
static void dma_program_48k(void)
{
    DMASRC0_L = 0x61;
    DMASRC0_M = 0xA8;
    DMASRC0_H = 0x0F;
    DMASRC2_L = 0x61;
    DMASRC2_M = 0xA8;
    DMASRC2_H = 0x0F;
}

static void dma_program_44k1(void)
{
    DMASRC0_L = 0x6A;
    DMASRC0_M = 0x4B;
    DMASRC0_H = 0x20;
    DMASRC2_L = 0x6A;
    DMASRC2_M = 0x4B;
    DMASRC2_H = 0x20;
}

void streaming_set_rate(unsigned long hz)
{
    stream_rate = hz;

    /* Prelude — seed both adaptive-clock-generator digital control
     * registers with 0x10, exactly as Rev 20 does.
     *
     * 0xFFE2 is ACGDCTL and 0xFFF6 is ACG2DCTL per TI Reg_stc1.h. This
     * previously read `XDATA(0xFFE2) = 0x00` with the comment "Rev-20
     * DMACTL2 halt", citing rev20_dynamic_reconfig.md §2's line
     * `DMACTL2 = 0`. Both halves of that were wrong:
     *   - 0xFFE2 is not a DMA register at all. Our regs.h alias
     *     "DMACTL2" for it was invented; TI names it ACGDCTL.
     *   - Rev 20 never writes 0x00 there. Its only reference to 0xFFE2
     *     is at 0x0736 (MOV DPTR,#0xFFE2) followed by LCALL 0x0E18,
     *     and 0x0E18 is `MOV A,#0x10 / MOVX @DPTR,A / MOV DPTR,#0xFFF6
     *     / MOVX @DPTR,A / RET` — it writes 0x10 to BOTH ACG registers.
     *     Verified by scanning rev20_firmware_code.bin for every
     *     occurrence of the byte sequence 90 FF E2; there is exactly one.
     *
     * Writing 0 to a clock-generator control register instead of 0x10
     * would misconfigure the capture clock on every rate change. */
    ACGDCTL  = 0x10;   /* Rev 20 fcn.0x0E18 @ 0x0E18 */
    ACG2DCTL = 0x10;   /* Rev 20 fcn.0x0E18 @ 0x0E1B */

    if (hz == 48000UL) {
        /* mode 2 — 48 kHz internal. All 6 writes: Rev 20 fcn.0x0728
         * @ 0x0771 mode-2 branch. See rev20_dynamic_reconfig.md §3. */
        DMASRC0_L = 0x6A;  /* Rev 20 fcn.0x0728 @ 0x0771 */
        DMASRC0_M = 0x4B;  /* Rev 20 fcn.0x0728 @ 0x0773 */
        DMASRC0_H = 0x20;  /* Rev 20 fcn.0x0728 @ 0x0775 */
        DMASRC2_L = 0x6A;  /* Rev 20 fcn.0x0728 @ 0x0777 */
        DMASRC2_M = 0x4B;  /* Rev 20 fcn.0x0728 @ 0x0779 */
        DMASRC2_H = 0x20;  /* Rev 20 fcn.0x0728 @ 0x077B */

        g_codec_state_23 |= (unsigned char)0x0C;   /* 48 kHz codec bits */
    } else {
        /* mode 1 — 44.1 kHz internal.
         * Rev 20 (rev20_flat.asm 0x075F) does NOT write DMASRC0/2 here;
         * it writes only DMACTL1 = 0x0D and relies on the power-on
         * DMASRC defaults (which happen to be the 44.1 constants).
         * Cite: rev20_dynamic_reconfig.md §3 row "44.1k mode 1"
         * ("mode 1 does NOT write DMASRC").
         * Rev 20 fcn.0x0728 @ 0x075F writes 0x0D to Rev-20-empirical
         * DMACTL1. */
        XDATA(0xFFE1) = 0x0D;             /* Rev-20 DMACTL1 mode-1 */

        g_codec_state_23 &= (unsigned char)~0x0C;  /* 44.1 kHz codec bits */
    }

    /* Common tail — same for both rates.
     * Cite: rev20_dynamic_reconfig.md §3 "Common streaming tail"
     * (rev20_flat.asm 0x07C5-0x0803). Bullets:
     *   1. ACG2DCTL seed for second clock generator
     *   2. Zero EP BCTX/BSIZ (Rev 20 clears these on stream re-arm)
     *   3. Enable both streaming EPs (0xC5 = enable + ISO + buffer valid)
     *   4. Arm DMA channels 0+1 via DMACTL1 |= 0xC0 (bit 6 + bit 7) */
    XDATA(0xFFF6) = 0x10;   /* TI ACG2DCTL — Rev 20 fcn.0x0728 @ 0x07C5 */
    IEPBCTX1 = 0;           /* Rev 20 fcn.0x0728 tail — clear stream EPs */
    IEPBSIZ1 = 0;           /* Rev 20 fcn.0x0728 tail */
    OEPBCTX2 = 0;           /* Rev 20 fcn.0x0728 tail */
    OEPBSIZ2 = 0;           /* Rev 20 fcn.0x0728 tail */
    IEPCNF1  = 0xC5;        /* Rev 20 fcn.0x0728 @ 0x07EA — enable EP1 IN */
    OEPCNF2  = 0xC5;        /* Rev 20 fcn.0x0728 @ 0x07F0 — enable EP2 OUT */
    XDATA(0xFFE1) |= 0xC0;  /* Rev 20 fcn.0x0728 @ 0x07E0 — arm DMA 0+1 */

    /* Codec-side state commit. On real silicon fcn.0x0E74 is dead
     * code (codec self-configures from I²S clocks — see NOTES.md),
     * so this is effectively a no-op, but we keep it to match Rev 20
     * behaviour byte-for-byte in case a future codec revision does
     * listen to the shift-in port. */
    codec_commit();
}

void streaming_playback_enable(unsigned char on)
{
    if (on) {
        OEPBBAX2 = EP_BBAX(EP2_OUT_BUF_ADDR);
        OEPBSIZ2 = EP_BSIZE(EP_AUDIO_BUF_SIZE);
        OEPBCTX2 = 0;
        /* Rev 20 writes 0xC5 = enable + isochronous + buffer valid. */
        OEPCNF2  = 0xC5;
    } else {
        OEPCNF2  = 0;
    }
}

void streaming_capture_enable(unsigned char on)
{
    if (on) {
        IEPBBAX1 = EP_BBAX(EP1_IN_BUF_ADDR);
        IEPBSIZ1 = EP_BSIZE(EP_AUDIO_BUF_SIZE);
        IEPBCTX1 = 0;
        IEPCNF1  = 0xC5;
    } else {
        IEPCNF1  = 0;
    }
}

/* SOF-tick service — called from usb_service() when VEC_SOF fires.
 *
 * Deliberately a no-op. Rev 20's Timer 0 ISR at 0x101E turned out to be
 * a 9-byte "just set a pending flag" stub (`clr EA; setb 0x24.0; reload
 * TH0; setb EA; reti`). The actual audio buffer shuttling is done in
 * hardware by the TAS1020A's DMA engine, which autoruns between the C-port
 * (I²S) and USB packet memory once endpoints are enabled. Our polling
 * usb_service() loop is the direct equivalent of Rev 20's "check pending
 * flag" idiom — no per-SOF work needed.
 *
 * If we later add async endpoint feedback or drift correction (both
 * useful upgrades over Digi's adaptive-sync design), that logic lands
 * here.
 */
void streaming_sof(void)
{
}
