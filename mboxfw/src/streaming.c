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

/* Currently-active sample rate — mirrors g_sample_rate in usb.c. */
static __data unsigned long stream_rate = 48000UL;

/* Rev 20's fcn.0x0DEC values (48 kHz path). */
static void dma_program_48k(void)
{
    DMASRC0_L = 0x61;
    DMASRC0_M = 0xA8;
    DMASRC0_H = 0x0F;
    DMASRC2_L = 0x61;
    DMASRC2_M = 0xA8;
    DMASRC2_H = 0x0F;
}

/* TODO: capture Rev 20's 44.1 kHz DMA values (they live in the mode-1
 * branch of fcn.0x0728, not fcn.0x0DEC). For now, mirror the 48k path
 * and rely on the CS8427 clock to divide down — audio will be slightly
 * wrong until we plug the real numbers in. */
static void dma_program_44k1(void)
{
    dma_program_48k();
}

void streaming_set_rate(unsigned long hz)
{
    stream_rate = hz;
    if (hz == 44100UL) {
        dma_program_44k1();
    } else {
        dma_program_48k();
    }
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
 * At Full Speed there's one SOF per ms, matching one iso packet per EP. */
void streaming_sof(void)
{
    /* TODO: swap DMA source/dest between the two halves of a double
     * buffer so the DMA engine always has one full buffer to work on
     * while USB fills / drains the other. This is where the actual
     * audio glue lives — Rev 20 handles it inside its Timer 0 ISR
     * (fcn.0x101E, which we haven't fully mapped yet). */
}
