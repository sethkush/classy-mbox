/*
 * Telemetry counters and block reader. See mboxfw/TELEMETRY.md for the
 * block map and the reasoning behind the single-packet constraint.
 */

#include "regs.h"
#include "telemetry.h"
#include "mux.h"
#include "codec.h"
#include "cs8427.h"
#include "streaming.h"   /* g_clock_mode, reported in block 9 */

volatile __data unsigned int  tlm_setup_count = 0;
volatile __data unsigned int  tlm_iep0_count  = 0;
volatile __data unsigned int  tlm_chunks      = 0;
volatile __data unsigned int  tlm_drains      = 0;
volatile __data unsigned int  tlm_rstr_count  = 0;
volatile __data unsigned int  tlm_loop_count  = 0;
volatile __data unsigned char tlm_stalls      = 0;
volatile __data unsigned char tlm_mux_sets    = 0;
volatile __data unsigned char tlm_mux_rejects = 0;
volatile __data unsigned char tlm_stage       = 0;
volatile __data unsigned char tlm_phases      = 0;

volatile __data unsigned char tlm_last_bmreq  = 0;
volatile __data unsigned char tlm_last_breq   = 0xEE;  /* 0xEE = no SETUP yet */
volatile __data unsigned int  tlm_last_wvalue = 0;
volatile __data unsigned int  tlm_last_windex = 0;
volatile __data unsigned int  tlm_last_wlength = 0;


volatile __data unsigned char tlm_eeprom_ok     = 0xFF;  /* 0xFF = not run */
volatile __data unsigned char tlm_cs8427_status = 0xFF;
volatile __data unsigned char tlm_codec_status  = 0xFF;

volatile __data unsigned int  tlm_sof_count = 0;
volatile __data unsigned char tlm_vec_iep1  = 0;
volatile __data unsigned char tlm_vec_oep2  = 0;
volatile __data unsigned char tlm_last_iface = 0xFF;  /* 0xFF = none seen */
volatile __data unsigned char tlm_last_alt   = 0xFF;
volatile __data unsigned char tlm_alt_seen   = 0;

/* 0xFF = not sampled, so a block-8 read of all-0xFF means main() never ran
 * that far rather than "the boot ROM left everything at 0xFF". */



/* Little-endian 16-bit store, matching how the host unpacks the blocks. */
static void put16(unsigned char __data *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)(v >> 8);
}

unsigned char tlm_read_block(unsigned char index, unsigned char __data *out)
{
    unsigned char i;

    switch (index) {
    case 0:
        /* Identity and liveness — "what is running, and is it?" */
        put16(&out[0], TLM_BUILD_ID);
        out[2] = tlm_stage;
        out[3] = tlm_phases;
        put16(&out[4], tlm_loop_count);
        put16(&out[6], tlm_rstr_count);
        return 1;

    case 1:
        /* EP0 continuation forensics — the reason this exists.
         *
         * For an N-packet reply the device should take N IEP0 interrupts
         * and push N chunks. chunks falling short of what the SETUPs
         * asked for means the device stopped being asked: a lost
         * interrupt. drains lagging chunks means transfers are being
         * abandoned somewhere else. Host-side timeouts cannot separate
         * those two; this can. */
        put16(&out[0], tlm_setup_count);
        put16(&out[2], tlm_iep0_count);
        put16(&out[4], tlm_chunks);
        put16(&out[6], tlm_drains);
        return 1;

    case 2:
        /* Last SETUP seen — "which request is failing?" */
        out[0] = tlm_last_bmreq;
        out[1] = tlm_last_breq;
        put16(&out[2], tlm_last_wvalue);
        put16(&out[4], tlm_last_windex);
        put16(&out[6], tlm_last_wlength);
        return 1;

    /* case 3 (#46 endpoint geometry + DMABCNT0 + playback resyncs) RETIRED
     * 2026-08-05 with the 88.2/96 kHz support it was built for. It existed to
     * tell a starved playback buffer from a thrashing SOF watchdog at 96 kHz,
     * and it did exactly that -- resyncs read 0 at 48 kHz and saturated at 96,
     * which is what identified the two as separate defects. With the doubled
     * rates gone the geometry is stock's again and the watchdog never
     * evaluates at 44.1/48, so there is nothing left for it to discriminate.
     * Block 6 still carries the live DMA state. */
    case 4:
        /* Peripheral results — separates a hardware fault from a
         * firmware fault without a scope. */
        out[0] = tlm_eeprom_ok;
        out[1] = tlm_cs8427_status;
        out[2] = tlm_codec_status;
        out[3] = tlm_stalls;
        /* Bytes 4-5 are LIVE port reads, sampled at the moment the host
         * asks. Hold the button while reading block 4 to see which bit moves.
         * Bytes 6-7 carried the handoff samples until 0x002B; see telemetry.h
         * for why they went and what replaced them. They now read 0. */
        out[4] = P1;
        out[5] = P3;
        return 1;

    case 5:
        /* Isochronous streaming state. Bytes 4-7 are LIVE register reads,
         * so a host can watch the endpoint config and byte counts change
         * (or fail to) while arecord is running. */
        put16(&out[0], tlm_sof_count);
        out[2] = tlm_vec_iep1;
        out[3] = tlm_vec_oep2;
        out[4] = IEPCNF1;
        out[5] = OEPCNF2;
        out[6] = tlm_alt_seen;
        out[7] = (unsigned char)((tlm_last_iface << 4) | (tlm_last_alt & 0x0F));
        return 1;

    case 6:
        /* DMA and C-port live state — the isoc data source.
         *
         * SOF fires and the endpoints are enabled, but tlm_vec_iep1 stays
         * 0: the device never returns a packet for an IN token. For
         * isochronous there is no NAK, so that means the endpoint buffer
         * is never filled. streaming_set_rate() arms IEPDCNTX1 = 0 and
         * relies on the DMA engine to fill it from the C-port; this block
         * shows whether either half is actually running.
         *
         * NOTE: bytes 0-1 used to read XDATA(0xFFE1) under the name
         * DMACTL1 and treat 0xC0 there as proof the DMA was armed. 0xFFE1
         * is ACGCTL, a clock-generator register; the reading was real but
         * meant nothing about DMA. The real channel control registers are
         * DMACTL0 (0xFFE8) and DMACTL1 (0xFFEE), and bit 7 (DMAEN) is the
         * bit that matters. Expect 0x89 / 0x82 while streaming.
         *
         * byte 2 is CPTCTL (0xFFDC), a control-AND-status register --
         * datasheet §6.5.4.5. It reads back as the R/W control bits hw_init
         * wrote (RXIE|TXIE = 0x50) OR'd with whatever the read-only status
         * bits RXF (7) and TXE (5) currently say, which is why 0x70 is the
         * normal reading and not a discrepancy. #164.
         *
         * This used to carry a caution that reading it might consume
         * clear-on-read bits. The datasheet refutes that: RXF is cleared by
         * reading the receive DATA register and TXE by writing the transmit
         * data register, neither of which is this address. The caution was
         * invented by the old name CPTSTA. Reading here is side-effect free. */
        out[0] = DMACTL1;   /* capture channel — bit 7 = DMAEN */
        out[1] = DMACTL0;   /* playback channel */
        out[2] = CPTCTL;
        out[3] = ACGCTL;
        /* out[4] carried IEPCNF1 until 0x002C. Block 5 byte 4 is the SAME
         * register read the same way, and the duplicate cost ~7 bytes that the
         * SOF watchdog's two-sighting rule needed. Reads 0 now. */
        out[5] = IEPDCNTX1;
        out[6] = IEPBSIZ1;
        out[7] = OEPDCNTX2;
        return 1;

    /* case 7 (EP0 buffer counts + suspend tally) RETIRED 2026-08-05 for the
     * code budget. Its own text already recorded why it had stopped earning
     * its place: usb_ep0_setup() clears both EP0 Y counts now, "so a read here
     * shows 0 whether or not the residue was ever there". #148 settled the
     * Y-count question and #149/#175 settled suspend/resume.
     *
     * The ~12% geometric EP0 IN loss it was built to chase is still open, but
     * this block is no longer the instrument for it -- block 1's chunk/drain
     * accounting is, and that stays. */
    /* case 8 (boot-ROM handoff snapshot) RETIRED 2026-08-03. It answered
     * WHAT_REMAINS_UNKNOWN.md §3a -- the ROM DOES leave an EP0 Y count
     * non-zero -- and confirmed GLOBCTL = 0x04 at handoff, on this part. Both
     * results are recorded; the block was carrying the code to re-measure a
     * settled question. The INDEX is left in place and falls through to the
     * 0xFF sentinel rather than being reused, so a host tool from before this
     * build reads "not served" instead of decoding block 9's panel state as a
     * handoff snapshot. */
    case 9:
        /* Panel state — which source is actually selected, right now.
         *
         * Until this block existed there was no way to ask. Block 4 offers live
         * P1/P3 reads but not the published mux word, so on 2026-07-29 the
         * selected source was established only by Seth reading the front-panel
         * LEDs in person, after the fact — and it turned out to be mic on both
         * channels while the loopback fed a line input, which voided the whole
         * session. A capture measurement that cannot state its own input
         * routing cannot be trusted, so this block is read alongside every one.
         *
         * Bytes 0-3 are the two published words: the 8-bit panel/mux chain
         * (RAM[0x22]) and the 16-bit codec chain (RAM[0x23]:RAM[0x25]).
         * Together they are the complete state of both latch chains, so a host
         * can also confirm the spdif/USB/mono LEDs went dark as codec_init()
         * intends. */
        out[0] = g_mux_state;
        out[1] = MONO_IS_SET() ? 1 : 0;
        out[2] = g_codec_state_23;
        out[3] = g_codec_state_25;
        /* Live P3: the three button pins, so a press is visible over the wire
         * without anyone watching the panel. */
        out[4] = P3;
        out[5] = tlm_mux_sets;
        out[6] = tlm_mux_rejects;
        /* Applied clock mode, stock's RAM[0x08] numbering: 1 = slaved to
         * S/PDIF, 2 = internal 44.1 kHz, 3 = internal 48 kHz (#177). Byte 3
         * already carries the Selector position as bit 0x25.4, so these two
         * together answer "what is it routed to, and what is clocking it" —
         * the pair that has to agree for S/PDIF input to work at all. */
        out[7] = g_clock_mode;
        return 1;

    /* case 10 (CS8427 read-back probe, #165) RETIRED 2026-08-03. It answered
     * its question -- no P3 pin ever varied across the eight read clocks, so
     * CDOUT is not readable here -- and the answer does not change by asking
     * again. Index left unused, same reasoning as case 8. */
    default:
        /* Clean sentinel rather than a stall, so a host walking blocks
         * until it runs out gets a defined answer. */
        for (i = 0; i < TLM_BLOCK_SIZE; i++) out[i] = 0xFF;
        return 0;
    }
}

void tlm_reset_counters(void)
{
    /* tlm_boot_handoff is deliberately NOT reset here, and must not be added.
     * It is a one-time observation of what the boot ROM handed us, taken before
     * main() writes anything; zeroing it on a counter reset would turn a real
     * measurement into a plausible-looking zero, which is the exact failure this
     * block exists to avoid. The P1/P3 handoff samples were omitted for the same
     * reason before they were retired entirely in 0x002B. */
    tlm_setup_count = 0;
    tlm_iep0_count  = 0;
    tlm_chunks      = 0;
    tlm_drains      = 0;
    tlm_rstr_count  = 0;
    tlm_stalls      = 0;
    tlm_sof_count = 0;
    tlm_vec_iep1  = 0;
    tlm_vec_oep2  = 0;
    tlm_alt_seen  = 0;   /* per-experiment, so a reset isolates one run */
    /* tlm_stage, tlm_phases, tlm_loop_count and the peripheral results are
     * deliberately NOT cleared: they describe how this boot went, not the
     * current experiment, and clearing them would destroy the only record
     * of an init failure. */
}
