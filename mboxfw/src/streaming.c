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
    if (hz == 44100UL) {
        dma_program_44k1();
        /* Rev 20's fcn.0x0728 44.1 kHz branch clears RAM[0x23].2 and .3
         * (rev20_flat.asm:940-941 @ 0x0741). These bits are the codec's
         * per-rate config nibble — leaving them wrong when the host
         * SET_CURs 44.1 kHz produces silent or wrong-pitched output. */
        g_codec_state_23 &= (unsigned char)~0x0C;
    } else {
        dma_program_48k();
        /* 48 kHz branch @ 0x0800 sets both bits (rev20_flat.asm:1030-1031). */
        g_codec_state_23 |= (unsigned char)0x0C;
    }
    /* Shift the updated control word to the codec chip so the change
     * actually takes effect. Rev 20's mode-switch flow calls the
     * adjuster + shift after each per-mode bit tweak; codec_commit()
     * bundles both. */
    codec_commit();

    /* Sample-rate change usually also implies a CS8427 clock-source
     * update. Wire that through fcn.0x080B-style register writes once
     * the codec side is refined. TODO: cs8427_set_rate(hz); */
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
