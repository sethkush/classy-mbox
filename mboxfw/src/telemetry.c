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



/* #205. RELEASE builds keep block 0 and nothing else.
 *
 * Stripping ALL field visibility to save ~60 bytes out of 1120 is a bad trade.
 * "Which firmware is running" is the one question you cannot debug anything
 * without, and on a part where one power cycle buys one image and costs a 2 km
 * round trip, answering it over the wire is worth more than the bytes.
 *
 * What survives is honest about what it can still see. The stage ladder and all
 * but one of the saturating counters ARE compiled out, so their bytes read zero
 * rather than stale values -- and byte 2 is documented as "not measured in
 * release" instead of being filled with something that looks like a reading.
 * tlm_phases survives because main.c sets it with plain ORs that are not part of
 * the diagnostic surface, so the boot-phase mask is real in every build.
 *
 * The exception is rstr_count, added to release on 2026-08-16 when #214 pushed
 * the diagnostic build past 6016 and the release tier became the shipping one.
 * It is the only way to tell a bus reset from a cold boot -- the proof is a
 * counter going DOWN -- and without it every future power-cycle question would
 * need a diagnostic build flashed first, spending a power cycle to ask a
 * question about power cycles. 28 bytes. */
#ifdef MBOX_RELEASE
unsigned char tlm_read_block(unsigned char index, unsigned char __data *out)
{
    unsigned char i;

    for (i = 0; i < TLM_BLOCK_SIZE; i++) {
        out[i] = 0xFF;            /* the unknown-block sentinel */
    }
    if (index != 0) {
        return 0;
    }
    out[0] = (unsigned char)(TLM_BUILD_ID & 0xFF);
    out[1] = (unsigned char)(TLM_BUILD_ID >> 8);
    out[2] = 0;                   /* stage ladder: not measured in release */
    out[3] = tlm_phases;          /* real -- plain ORs in main.c */
    out[4] = 0;
    out[5] = 0;                   /* loop_count: not measured in release */
    /* rstr_count IS measured in release. Same bytes 6-7 as the diagnostic
     * build, so mboxtlm.py's block 0 decoder needs no tier awareness. It is
     * the only counter that survives -- see TLM_INC16_KEEP in telemetry.h. */
    out[6] = (unsigned char)(tlm.rstr_count & 0xFF);
    out[7] = (unsigned char)(tlm.rstr_count >> 8);
    return 1;
}
#endif

#ifndef MBOX_RELEASE   /* RELEASE: the block reader and the counter
                       * reset are the bulk of the diagnostic surface. */
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

#ifdef MBOX_TLM_FULL
    case 1:
        /* EP0 continuation forensics — the reason this exists.
         *
         * For an N-packet CHUNKED reply the device takes N IEP0 interrupts
         * and pushes N chunks. chunks falling short of what the SETUPs
         * asked for means the device stopped being asked: a lost
         * interrupt. drains lagging chunks means transfers are being
         * abandoned somewhere else. Host-side timeouts cannot separate
         * those two; this can.
         *
         * READ THE COUNTERS RIGHT. `chunks` counts push_reply_chunk() only,
         * so it counts the CHUNKED path and nothing else. Single-packet
         * replies go through stage_immediate(), which writes the EP0 buffer
         * directly and never increments it — and every telemetry read is one
         * of those, as is the zero-length IN status stage of every no-data
         * control write. So `iep0 > chunks` always, on a completely healthy
         * device, and the gap widens just by polling this block.
         *
         * mboxtlm.py used to flag that gap as "pushes are being missed". It
         * fired on every reading for weeks and meant nothing; the rule is now
         * `chunks && !drains` instead. Measured 2026-08-11: 10 reads gave
         * iep0 +11 / chunks +0, 10 no-data writes gave iep0 +11 / chunks +0,
         * and 50 reads of the 238-byte config descriptor gave 0 short reads
         * with chunks +1516 against a floor of +1500. */
        put16(&out[0], tlm.setup_count);
        put16(&out[2], tlm.iep0_count);
        put16(&out[4], tlm.chunks);
        put16(&out[6], tlm.drains);
        return 1;
#endif

#ifdef MBOX_TLM_FULL
    case 2:
        /* Last SETUP seen — "which request is failing?" */
        out[0] = tlm_last_bmreq;
        out[1] = tlm_last_breq;
        put16(&out[2], tlm_last_wvalue);
        put16(&out[4], tlm_last_windex);
        put16(&out[6], tlm_last_wlength);
        return 1;
#endif

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
#ifdef MBOX_TLM_ROUTING
#ifdef MBOX_TLM_STALL
    case 4:
        /* #209. Block 4 restored to its original subject -- stalls -- after
         * being retired in 0x0037. The retirement note said restoring it was
         * "one struct byte plus one TLM_INC8"; it was two bytes and two, since
         * the counter alone cannot distinguish "we stalled it" from "we never
         * saw it".
         *
         * Bytes 2-7 are left at the 0xFF sentinel rather than padded with
         * zeros: a zero here would be indistinguishable from a real measured
         * zero, and rows 2 and 3 of the reading table are both genuine zeros. */
        out[0] = tlm.stalls;
        out[1] = tlm.gd_wlen0;
        out[2] = 0xFF;
        out[3] = 0xFF;
        out[4] = 0xFF;
        out[5] = 0xFF;
        out[6] = 0xFF;
        out[7] = 0xFF;
        return 1;
#endif

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
#endif

    /* case 11 (ACG clock measurement, #186 stage 1) RETIRED to pay for #203's
     * Selector Unit extension. Its question is closed: the measurement decided
     * the feedback-endpoint design (FINDING_186_ti_softpll_is_the_feedback_
     * endpoint.md), proved MCLKO runs at 256 fs to within 7 ppm, and proved
     * MCLKO keeps running with no stream open -- which is what made
     * FINDING_202's "the C-port receive side frames separately" reading
     * possible.
     *
     * streaming_acg_sample() STAYS and is not diagnostic. It is the source of
     * the feedback endpoint's 10.14 value, so removing the block removes only
     * the readback, not the measurement. That is the whole reason this block
     * was the right one to retire rather than block 0, 1, 2 or 9.
     *
     * tlm_fb_rejects loses its only reader here. Left in place rather than
     * removed: it is one byte of DATA, DATA is not the constrained resource,
     * and #186's rejection logic reads it internally. It is NOT a write-only
     * counter of the kind block 4's retirement had to avoid -- that trap was
     * about counters costing CODE at every increment for no reader.
     *
     * The index is NOT reused. */

    /* case 12 (#200 diagnostic state) RETIRED the same day it was added. Its
     * job was to confirm a stimulus had fired, and the captured audio does that
     * better: a run of ~8800 leading zeros IS a calibration, and its absence is
     * the absence of one. Reclaimed for #201. Index not reused. */

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
#endif  /* !MBOX_RELEASE */
