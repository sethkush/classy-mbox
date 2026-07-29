/*
 * Telemetry counters and block reader. See mboxfw/TELEMETRY.md for the
 * block map and the reasoning behind the single-packet constraint.
 */

#include "regs.h"
#include "telemetry.h"

volatile __data unsigned int  tlm_setup_count = 0;
volatile __data unsigned int  tlm_iep0_count  = 0;
volatile __data unsigned int  tlm_chunks      = 0;
volatile __data unsigned int  tlm_drains      = 0;
volatile __data unsigned int  tlm_rstr_count  = 0;
volatile __data unsigned int  tlm_loop_count  = 0;
volatile __data unsigned char tlm_stalls      = 0;
volatile __data unsigned char tlm_stage       = 0;
volatile __data unsigned char tlm_phases      = 0;

volatile __data unsigned char tlm_last_bmreq  = 0;
volatile __data unsigned char tlm_last_breq   = 0xEE;  /* 0xEE = no SETUP yet */
volatile __data unsigned int  tlm_last_wvalue = 0;
volatile __data unsigned int  tlm_last_windex = 0;
volatile __data unsigned int  tlm_last_wlength = 0;

volatile __data unsigned char tlm_vec_setup = 0;
volatile __data unsigned char tlm_vec_iep0  = 0;
volatile __data unsigned char tlm_vec_oep0  = 0;
volatile __data unsigned char tlm_vec_rstr  = 0;
volatile __data unsigned char tlm_vec_none  = 0;
volatile __data unsigned char tlm_vec_other = 0;

volatile __data unsigned char tlm_eeprom_ok     = 0xFF;  /* 0xFF = not run */
volatile __data unsigned char tlm_cs8427_status = 0xFF;
volatile __data unsigned char tlm_codec_status  = 0xFF;

volatile __data unsigned int  tlm_sof_count = 0;
volatile __data unsigned char tlm_vec_iep1  = 0;
volatile __data unsigned char tlm_vec_oep2  = 0;
volatile __data unsigned char tlm_last_iface = 0xFF;  /* 0xFF = none seen */
volatile __data unsigned char tlm_last_alt   = 0xFF;
volatile __data unsigned char tlm_alt_seen   = 0;

volatile __data unsigned char tlm_p1_boot = 0xFF;  /* 0xFF = not sampled */
volatile __data unsigned char tlm_p3_boot = 0xFF;

/* Little-endian 16-bit store, matching how the host unpacks the blocks. */
static void put16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)(v >> 8);
}

unsigned char tlm_read_block(unsigned char index, unsigned char *out)
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

    case 3:
        /* VECINT histogram. A large `none` means the ISR is firing
         * spuriously; a nonzero `other` means a vector we do not handle
         * is arriving. */
        out[0] = tlm_vec_setup;
        out[1] = tlm_vec_iep0;
        out[2] = tlm_vec_oep0;
        out[3] = tlm_vec_rstr;
        out[4] = tlm_vec_none;
        out[5] = tlm_vec_other;
        out[6] = 0;
        out[7] = 0;
        return 1;

    case 4:
        /* Peripheral results — separates a hardware fault from a
         * firmware fault without a scope. */
        out[0] = tlm_eeprom_ok;
        out[1] = tlm_cs8427_status;
        out[2] = tlm_codec_status;
        out[3] = tlm_stalls;
        /* Bytes 4-5 are LIVE port reads, sampled at the moment the host
         * asks. Hold the button while reading block 4 and compare against
         * the boot samples in 6-7 to see which bit actually moves. */
        out[4] = P1;
        out[5] = P3;
        out[6] = tlm_p1_boot;
        out[7] = tlm_p3_boot;
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
         * CAUTION: if CPTSTA has clear-on-read bits, reading it here
         * consumes them. Reads are only issued when a host explicitly
         * asks for block 6, never in the normal path. */
        out[0] = DMACTL1;   /* capture channel — bit 7 = DMAEN */
        out[1] = DMACTL0;   /* playback channel */
        out[2] = CPTSTA;
        out[3] = ACGCTL;
        out[4] = IEPCNF1;
        out[5] = IEPDCNTX1;
        out[6] = IEPBSIZ1;
        out[7] = OEPDCNTX2;
        return 1;

    default:
        /* Clean sentinel rather than a stall, so a host walking blocks
         * until it runs out gets a defined answer. */
        for (i = 0; i < TLM_BLOCK_SIZE; i++) out[i] = 0xFF;
        return 0;
    }
}

void tlm_reset_counters(void)
{
    tlm_setup_count = 0;
    tlm_iep0_count  = 0;
    tlm_chunks      = 0;
    tlm_drains      = 0;
    tlm_rstr_count  = 0;
    tlm_stalls      = 0;
    tlm_vec_setup = 0;
    tlm_vec_iep0  = 0;
    tlm_vec_oep0  = 0;
    tlm_vec_rstr  = 0;
    tlm_vec_none  = 0;
    tlm_vec_other = 0;
    tlm_sof_count = 0;
    tlm_vec_iep1  = 0;
    tlm_vec_oep2  = 0;
    tlm_alt_seen  = 0;   /* per-experiment, so a reset isolates one run */
    /* tlm_stage, tlm_phases, tlm_loop_count and the peripheral results are
     * deliberately NOT cleared: they describe how this boot went, not the
     * current experiment, and clearing them would destroy the only record
     * of an init failure. */
}
