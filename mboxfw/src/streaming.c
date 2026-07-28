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
 * The two 24-bit constants Rev 20 loads (0x0F_A861 at fcn.0x0DEC/0x0DFE,
 * 0x20_4B6A at fcn.0x0728 @ 0x0771) are ADAPTIVE CLOCK GENERATOR frequency
 * words — ACG1FRQ2/1/0 at 0xFFE5-7 and ACG2FRQ2/1/0 at 0xFFF7-9, per TI
 * Reg_stc1.h and datasheet §6.5.3. They were previously named DMASRC0/2
 * here, which was invented; there is no DMA source register on this part.
 * The two dead helpers dma_program_48k()/dma_program_44k1() that wrapped
 * them are deleted — they were never called, and their names asserted a
 * rate mapping that the live code below contradicts.
 *
 * Which constant belongs to which rate is still unsettled and is a PITCH
 * question, not a data-flow one; settle it with a loopback measurement.
 */
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
        ACG1FRQ1 = 0x4B;  /* Rev 20 fcn.0x0728 @ 0x0771 */
        ACG1FRQ2 = 0x6A;  /* Rev 20 fcn.0x0728 @ 0x0777 */
        ACG1FRQ0 = 0x20;  /* Rev 20 fcn.0x0728 @ 0x077D */
        ACG2FRQ1 = 0x4B;  /* Rev 20 fcn.0x0728 @ 0x0783 */
        ACG2FRQ2 = 0x6A;  /* Rev 20 fcn.0x0728 @ 0x0789 */
        ACG2FRQ0 = 0x20;  /* Rev 20 fcn.0x0728 @ 0x078F */

        g_codec_state_23 |= (unsigned char)0x0C;   /* 48 kHz codec bits */
    } else {
        /* mode 1 — 44.1 kHz internal.
         * Rev 20 (rev20_flat.asm 0x075F) does NOT write the ACG frequency
         * words here; it writes only ACGCTL = 0x0D and relies on the
         * power-on ACG defaults. */
        ACGCTL = 0x0D;    /* Rev 20 fcn.0x0728 @ 0x075F */

        g_codec_state_23 &= (unsigned char)~0x0C;  /* 44.1 kHz codec bits */
    }

    /* Common tail — same for both rates. Rev 20 fcn.0x0728 0x07C4-0x07FF.
     *
     * Rev 20 clears the four BYTE-COUNT registers (X and Y buffer counts
     * for both streaming endpoints) and nothing else. It does NOT touch
     * IEPBSIZ1/OEPBSIZ2 — those are the buffer SIZE registers, programmed
     * once in streaming_capture_enable()/streaming_playback_enable().
     * This code used to zero the SIZE registers instead of the Y counts,
     * which left whichever endpoint was already streaming with a
     * zero-length buffer if a SET_CUR(sample rate) arrived after
     * SET_INTERFACE. Rev 20 sets BBAX/BSIZ exactly once at 0x09B1-0x09C6
     * and never again. */
    ACG2DCTL = 0x10;        /* Rev 20 fcn.0x0728 @ 0x07C4 */
    IEPBCTX1 = 0;           /* Rev 20 fcn.0x0728 @ 0x07E5 */
    IEPBCTY1 = 0;           /* Rev 20 fcn.0x0728 @ 0x07EA */
    OEPBCTX2 = 0;           /* Rev 20 fcn.0x0728 @ 0x07EE */
    OEPBCTY2 = 0;           /* Rev 20 fcn.0x0728 @ 0x07F2 */
    IEPCNF1  = 0xC5;        /* Rev 20 fcn.0x0728 @ 0x07F6 — enable EP1 IN */
    OEPCNF2  = 0xC5;        /* Rev 20 fcn.0x0728 @ 0x07FC — enable EP2 OUT */
    /* ACGCTL bits 6-7, NOT a DMA arm. The old comment here claimed this
     * armed DMA channels 0+1; 0xFFE1 is the adaptive clock generator
     * control register (datasheet §6.5.3.11). The DMA channels are enabled
     * per-endpoint in streaming_capture_enable()/streaming_playback_enable()
     * via DMACTL1/DMACTL0 bit 7, which is what Rev 20 does at 0x03CF and
     * 0x03DF. */
    ACGCTL |= 0xC0;         /* Rev 20 fcn.0x0728 @ 0x07DE */

    /* Codec-side state commit. On real silicon fcn.0x0E74 is dead
     * code (codec self-configures from I²S clocks — see NOTES.md),
     * so this is effectively a no-op, but we keep it to match Rev 20
     * behaviour byte-for-byte in case a future codec revision does
     * listen to the shift-in port. */
    codec_commit();
}

/*
 * Enabling a streaming endpoint has TWO halves, and until 2026-07-28 this
 * firmware only did the first one.
 *
 *   1. Configure and enable the USB endpoint (BBAX/BSIZ/BCTX + xEPCNF).
 *   2. Set DMAEN (bit 7) in that endpoint's DMA channel control register,
 *      so the DMA engine actually moves bytes between the C-port and the
 *      endpoint buffer.
 *
 * Without (2) the endpoint is enabled and answers every IN token, but its
 * buffer is never filled, so the device returns a ZERO-LENGTH isochronous
 * packet each frame. That is exactly what usbmon measured on 2026-07-28
 * (ours: 0 bytes/frame; a stock Rev 18 unit on the same host: 288).
 *
 * Channel-to-endpoint mapping is fixed by the EPDIR/EPNUM fields hw_init
 * programs (see regs.h):
 *   DMACTL0 = 0x02 → OUT, EP2 → playback
 *   DMACTL1 = 0x09 → IN,  EP1 → capture
 * Rev 20 sets bit 7 of each at SET_INTERFACE time and clears it on stop:
 * capture at 0x03CF (set) / 0x033F and 0x03F1 (clear), playback at 0x03DF
 * and 0x043B (set) / 0x1013 (clear). We mirror that per-direction rather
 * than arming both channels together, so a host streaming in one direction
 * only does not leave the other channel running.
 */
void streaming_playback_enable(unsigned char on)
{
    if (on) {
        OEPBBAX2 = EP_BBAX(EP2_OUT_BUF_ADDR);
        OEPBSIZ2 = EP_BSIZE(EP_AUDIO_BUF_SIZE);
        OEPBCTX2 = 0;
        /* 0xC5 = IEPEN | ISO | BPS field 5 (6 bytes per sample, i.e.
         * stereo 24-bit) per datasheet §6.4.4.6.2. */
        OEPCNF2  = 0xC5;
        /* Rev 20 fcn.0x0398 @ 0x03DF — DMACTL0 |= DMAEN, after the
         * endpoint is enabled. Datasheet §6.5.2.3: "before enabling the
         * DMA channel, all other DMA channel configuration bits must be
         * set to the desired value." */
        DMACTL0 |= DMA_EN;
    } else {
        DMACTL0 &= (unsigned char)~DMA_EN;  /* Rev 20 fcn.0x1013 @ 0x1013 */
        OEPCNF2  = 0;
    }
}

void streaming_capture_enable(unsigned char on)
{
    if (on) {
        IEPBBAX1 = EP_BBAX(EP1_IN_BUF_ADDR);
        IEPBSIZ1 = EP_BSIZE(EP_AUDIO_BUF_SIZE);
        IEPBCTX1 = 0;
        /* 0xC5 = IEPEN | ISO | BPS field 5 (6 bytes per sample) per
         * datasheet §6.4.4.6.2. Rev 20 fcn.0x0398 @ 0x03C4 */
        IEPCNF1  = 0xC5;
        DMACTL1 |= DMA_EN; /* Rev 20 fcn.0x0398 @ 0x03CF */
    } else {
        DMACTL1 &= (unsigned char)~DMA_EN;  /* Rev 20 fcn.0x0330 @ 0x033F */
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
