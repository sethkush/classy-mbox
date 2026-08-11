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

volatile __data struct tlm_ctrs tlm;

/* #186 stage 1 — ACG clock measurement. Written only by
 * streaming_acg_sample() from the SOF interrupt. */
volatile __idata unsigned long tlm_acg_window = 0UL;
volatile __idata unsigned int  tlm_acg_last   = 0;
volatile __idata unsigned char tlm_acg_count  = 0;
volatile __idata unsigned char tlm_fb_rejects = 0;
volatile __data unsigned int  tlm_loop_count  = 0;
volatile __data unsigned char tlm_mux_sets    = 0;
volatile __data unsigned char tlm_mux_rejects = 0;
volatile __data unsigned char tlm_stage       = 0;
volatile __data unsigned char tlm_phases      = 0;

volatile __data unsigned char tlm_last_bmreq  = 0;
volatile __data unsigned char tlm_last_breq   = 0xEE;  /* 0xEE = no SETUP yet */
volatile __data unsigned int  tlm_last_wvalue = 0;
volatile __data unsigned int  tlm_last_windex = 0;
volatile __data unsigned int  tlm_last_wlength = 0;




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
        put16(&out[6], tlm.rstr_count);
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
        put16(&out[0], tlm.setup_count);
        put16(&out[2], tlm.iep0_count);
        put16(&out[4], tlm.chunks);
        put16(&out[6], tlm.drains);
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
    /* case 4 (stalls + live P1/P3) RETIRED 2026-08-06, build 0x0037, to make
     * room. It was the cheapest 32 bytes on the part that cost no capability
     * twice over: P3 is already reported by block 9 byte 4, and the stall
     * counter had exactly ONE reader -- this block -- so retiring the block
     * would have left a counter nobody could read. A write-only counter is
     * strictly worse than no counter: it costs the increment at every stall
     * site and reports nothing, which is the same defect as the three
     * never-written fields removed from this block on 2026-08-05, in mirror
     * image. So tlm.stalls went with it.
     *
     * WHAT THIS COSTS: #192 (USB20CV) deliberately issues unsupported
     * requests, and the stall counter was the device-side confirmation that we
     * STALLED rather than ACKed -- #188's whole subject. Restoring it is one
     * struct byte plus one TLM_INC8, so it can come back with #192 if the host
     * side turns out not to say enough on its own.
     *
     * The index is NOT reused. */
    /* case 5 (isochronous streaming state) RETIRED 2026-08-06, build 0x0039,
     * to pay for #197's capture-gate pulse. It was built for the era when
     * capture returned nothing: sof_count proved the SOF interrupt was masked
     * off (USBIMSK bit 4), and the live IEPCNF1/OEPCNF2 reads proved the
     * endpoints were configured while no packet came back. Both questions are
     * closed -- audio streams at both rates on both units, measured end to end
     * in FINDING_196 -- and block 2 still reports the last SETUP, which is what
     * shows a SET_INTERFACE arriving and with which alt.
     *
     * tlm.sof_count SURVIVES: block 11 byte 7 reads its low byte, so it keeps
     * a reader and does not become the write-only counter that block 4's
     * retirement had to avoid.
     *
     * tlm.vec_iep1, tlm.vec_oep2 and tlm.alt_seen had this block as their ONLY
     * reader, so they are removed with it, along with their increment sites in
     * usb.c. Keeping them would have cost the increment at every interrupt and
     * reported nothing -- exactly the defect that took tlm.stalls out with
     * block 4.
     *
     * The index is NOT reused. */
    /* case 6 (DMA + C-port live state) RETIRED 2026-08-05. It was built for
     * one question -- SOF fires and the endpoints are enabled, so why does the
     * device never return a packet for an IN token? -- and it answered it: the
     * bytes at 0xFFE1 that streaming.c believed were DMA channel enables are
     * ACGCTL, a clock-generator register, so the real DMA channels at
     * DMACTL0/DMACTL1 had never been armed and the capture buffer was never
     * filled. That is fixed and the isochronous path has since been measured
     * carrying correct audio at every rate this firmware supports.
     *
     * Block 5 still reports the endpoint config and the alt state, which is
     * what a streaming investigation starts from. Retired for the code budget
     * while the open question is EP0 multi-packet loss, which this block has
     * nothing to say about. */
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

    case 11:
        /* ACG clock measurement -- #186 stage 1.
         *
         * Bytes 0-3: the accumulated MCLK count for the last completed window.
         * Bytes 4-5: the last single-frame capture difference.
         * Byte 6: completed-window count, saturating -- proves the measurement
         * is live and lets a host confirm two reads span different windows.
         *
         * Byte 7: tlm_fb_rejects, the count of implausible windows discarded.
         *
         * THIS COMMENT USED TO SAY "low byte of tlm.sof_count", and the code
         * has never done that. Caught 2026-08-07 while trying to establish
         * whether SOF was counting: byte 7 read a constant 2 across a fast
         * poll, which reads exactly like a dead SOF counter and is in fact a
         * live reject count that had simply stopped changing. tlm.sof_count is
         * exposed in no block -- block 5 carried it and was retired -- which is
         * survivable now only because no SOF-based wait remains in the
         * firmware. Restore a reader before adding one.
         *
         * This block briefly carried #198 pulse state instead (build 0x0046),
         * to find out why a SOF wait never elapsed. It found it -- TLM_INC16
         * saturates -- and the ACG reporting comes back now that the pulse
         * machinery it was diagnosing has been deleted. */
        out[0] = (unsigned char)(tlm_acg_window & 0xFF);
        out[1] = (unsigned char)((tlm_acg_window >> 8) & 0xFF);
        out[2] = (unsigned char)((tlm_acg_window >> 16) & 0xFF);
        out[3] = (unsigned char)((tlm_acg_window >> 24) & 0xFF);
        put16(&out[4], tlm_acg_last);
        out[6] = tlm_acg_count;
        out[7] = tlm_fb_rejects;
        break;

    case 12:
        /* #200 diagnostic state. Reports what the firmware IS doing and what it
         * HAS done, so a null result can be told apart from a stimulus that
         * never fired -- four measurements were voided that way in one session.
         */
        out[0] = g_diag_clr_mask;
        out[1] = 0;
        out[2] = g_diag_rst_cycles;
        out[3] = 0;
        out[4] = 0;
        out[5] = 0;
        out[6] = 0;
        out[7] = 0;
        break;

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
    {
        unsigned char __data *p = (unsigned char __data *)&tlm;
        unsigned char i;
        for (i = 0; i < sizeof(tlm); i++)
            p[i] = 0;
    }
    /* tlm_stage, tlm_phases, tlm_loop_count and the peripheral results are
     * deliberately NOT cleared: they describe how this boot went, not the
     * current experiment, and clearing them would destroy the only record
     * of an init failure. */
}
