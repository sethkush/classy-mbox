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
#include "telemetry.h"
#include "mux.h"
#include "cs8427.h"

/* SOF watchdog shadow of the playback DMA buffer content — Rev 22's
 * RAM[0x1B]:RAM[0x1C]. 0xFF/0xFF is an impossible real count for a 512-byte
 * buffer, so it doubles as "no reading yet" and forces the first frame of a
 * stream to be evaluated. See streaming_sof(). */
static __data unsigned char sof_bcnt_hi = 0xFF;
static __data unsigned char sof_bcnt_lo = 0xFF;

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
    ACGDCTL  = 0x10;   /* Rev 20 fcn.0x0728 @ 0x0736 — the caller selects
                         * ACGDCTL (`90 ff e2`) and LCALLs the shared helper at
                         * 0x0E18, which writes the caller's DPTR then ACG2DCTL.
                         * Cite the site that names the register, not the helper. */
    ACG2DCTL = 0x10;   /* Rev 20 fcn.0x0E18 @ 0x0E1B */

    /* Adaptive clock generator.
     *
     * Rev 20's rate/clock dispatcher `fcn.0x0728` takes a mode in r7. Its
     * two stream-start call sites pass mode 3 (0x03CA capture, 0x0436
     * playback); modes 1/2/4/5 come from other request handlers. Mode 2 and
     * mode 3 differ ONLY in the 24-bit frequency word — both end at the
     * shared tail 0x0E0F..0x0E16, which writes ACGCTL = 0x06.
     *
     * ACGCTL bit 2 is DIVEN, the divide-by-I / divide-by-M enable
     * (datasheet §6.5.3.11, block diagram Figure 2-1). Every build before
     * 2026-07-28 left ACGCTL at 0xC0 with DIVEN clear, so the ÷M circuit was
     * off, neither MCLKO output ran, the codec was never clocked, no I2S
     * frame reached the C-port, and the DMA never cleared the NACK flag in
     * IEPDCNTX/Y. Per datasheet §2.2.7.4.1: "if an isochronous in token is
     * received when there is no new data to be output ... the UBM will
     * respond to the isochronous in request with a NULL packet" — precisely
     * the zero-length packets usbmon measured. Telemetry read ACGCTL back as
     * exactly 0xC0, confirming DIVEN was off.
     *
     * WHICH FREQUENCY WORD, measured rather than reasoned. A paired
     * experiment on two units, same host, same instant, one variable:
     *
     *   mode-2 word (0x6A/0x4B/0x20)  ->  DCNTX = 88 samples per USB frame
     *   mode-3 word (0x61/0xA8/0x0F)  ->  DCNTX = 96
     *
     * Both were exactly double a standard rate (88.2 = 2 x 44.1, 96 = 2 x
     * 48), because CPTRXCNF4 was set to DIVB2 = ÷2 where stock's boot init
     * uses ÷4 — see the long comment in hw_init.c. With ÷4 restored:
     *
     *   mode 3 -> 48 kHz      mode 2 -> 44.1 kHz
     *
     * which is consistent with Rev 20 passing mode 3 at SET_INTERFACE: 48
     * kHz is its default, and the host's SET_CUR selects a mode afterwards.
     *
     * This mapping has been wrong twice in this file, in both directions,
     * and each time the reasoning sounded fine. It is now anchored to a
     * measured sample count rather than to which Rev 20 branch looked most
     * relevant. If a rate ever comes out wrong again, read DCNTX first.
     */
    if (hz == 48000UL) {
        ACG1FRQ1 = 0xA8;  /* Rev 20 fcn.0x0DEC @ 0x0DEC — mode 3, 48 kHz */
        ACG1FRQ2 = 0x61;  /* Rev 20 fcn.0x0DEC @ 0x0DF2 */
        ACG1FRQ0 = 0x0F;  /* Rev 20 fcn.0x0DEC @ 0x0DF8 */
        ACG2FRQ1 = 0xA8;  /* Rev 20 fcn.0x0DEC @ 0x0DFE */
        ACG2FRQ2 = 0x61;  /* Rev 20 fcn.0x0DEC @ 0x0E04 */
        ACG2FRQ0 = 0x0F;  /* Rev 20 fcn.0x0DEC @ 0x0E0A */

    } else {
        ACG1FRQ2 = 0x6A;  /* Rev 20 fcn.0x0728 @ 0x0765 — mode 2, 44.1 kHz */
        ACG1FRQ1 = 0x4B;  /* Rev 20 fcn.0x0728 @ 0x075F */
        ACG1FRQ0 = 0x20;  /* Rev 20 fcn.0x0728 @ 0x076B */
        ACG2FRQ2 = 0x6A;  /* Rev 20 fcn.0x0728 @ 0x0777 */
        ACG2FRQ1 = 0x4B;  /* Rev 20 fcn.0x0728 @ 0x0771 */
        ACG2FRQ0 = 0x20;  /* Rev 20 fcn.0x0728 @ 0x077D */

    }
    /* Shared by both rates: Rev 20's modes 2 and 3 both end at the tail
     * 0x0E0F..0x0E16, which writes ACGCTL = 0x06 (DIVEN + both MCLKO
     * sourced from their synthesizers after ÷M). */
    ACGCTL = 0x06;        /* Rev 20 @ 0x0E10 */

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
    ACG2DCTL = 0x10;         /* Rev 20 fcn.0x0728 — CITATION UNVERIFIED, see note */
    IEPDCNTX1 = 0;           /* Rev 20 fcn.0x0728 @ 0x07D3 */
    IEPDCNTY1 = 0;           /* Rev 20 fcn.0x0728 @ 0x07D8 */
    OEPDCNTX2 = 0;           /* Rev 20 fcn.0x0728 @ 0x07DC */
    OEPDCNTY2 = 0;           /* Rev 20 fcn.0x0728 @ 0x07E0 */
    IEPCNF1  = 0xC5;         /* Rev 20 fcn.0x0728 @ 0x07E4 — enable EP1 IN */
    OEPCNF2  = 0xC5;         /* Rev 20 fcn.0x0728 @ 0x07EA — enable EP2 OUT */

    /* #147. Rev 20 sets IRAM 0x23.2 and 0x23.3 HERE, unconditionally, at
     * 0x07EE and 0x07F0 -- immediately after the two endpoint-enable writes
     * above and immediately before the chain-B commit at 0x07F2. rev22 is
     * identical at 0x07CF/0x07D1.
     *
     * This used to sit inside the rate branches: set for 48 kHz, CLEARED for
     * 44.1 kHz, commented "48 kHz codec bits". That reading is wrong. The only
     * writes to the pair in Rev 20's whole clock-mode routine are a CLR at the
     * top (0x072F/0x0731) and this SETB, and the SETB is straight-line code --
     * nothing XREFs 0x07EE, no branch skips it, and there is no RET anywhere
     * between 0x0728 and 0x07EE. Every mode reaches it and leaves both bits
     * set, whatever the rate. A rate selector would differ between the arms.
     *
     * The shape that is left -- cleared before the clock is disturbed, set
     * again only once the clock is stable AND the endpoints are re-armed, then
     * committed -- is an output mute or audio-path enable. Which is why this
     * mattered: at 44.1 kHz we were resting with both lines low, a state stock
     * never rests in, and if the mute reading is right we were muted at the
     * default Mac sample rate.
     *
     * See firmware_stock/decomp/FINDING_open_questions.md §1.8. The lines are
     * still not NAMED -- that needs the scope test -- but "not a rate selector"
     * is established from the bytes. */
    g_codec_state_23 |= (unsigned char)0x0C;
    /* ACGCTL bits 6-7, NOT a DMA arm. The old comment here claimed this
     * armed DMA channels 0+1; 0xFFE1 is the adaptive clock generator
     * control register (datasheet §6.5.3.11). The DMA channels are enabled
     * per-endpoint in streaming_capture_enable()/streaming_playback_enable()
     * via DMACTL1/DMACTL0 bit 7, which is what Rev 20 does at 0x03CF and
     * 0x03DF. */
    ACGCTL |= 0xC0;         /* Rev 20 fcn.0x0728 @ 0x07CC */

    /* Publish the codec word. This is Rev 20's `LCALL 0x0E62` at 0x07F2,
     * immediately after the SETB pair above — the unmute half of the bracket
     * that opened with CLR 0x23.2 / CLR 0x23.3 and its own LCALL 0x0E62 at
     * 0x072F-0x0733. Rev 22 at 0x07D6 / 0x0714.
     *
     * Was codec_commit(), which also republished the mux word and ran the
     * 0x22.6 derivation. Neither belongs on a clock-mode change: stock's
     * clock routine calls only 0x0E62, never 0x0F0C. */
    codec_write_word();
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
/* Non-zero while the host has playback selected (SET_INTERFACE alt=1 on the
 * playback interface). streaming_sof() gates on this -- see the note there. */
static __data unsigned char playback_running = 0;
static __data unsigned char capture_running  = 0;

/*
 * Panel bit 7 = "streaming active", asserted LOW.
 *
 * The panel/mux shift word RAM[0x22] is active-low throughout: each source
 * field holds exactly one bit low ("one-cold"), suspend writes 0xFF for
 * everything-off, and hw_init's first publish writes 0x00 as an all-on lamp
 * flash. See disasm/MUX_IRAM22_ANNOTATION.md, section "The byte is ACTIVE-LOW".
 *
 * Stock drives bit 7 from its SET_INTERFACE handler, one branch each way:
 *
 *   Rev 20 fcn.0x0386 @ 0x03A0  CLR  bit 0x17   ; stream START -> assert
 *   Rev 20 fcn.0x0386 @ 0x03E6  SETB bit 0x17   ; stream STOP  -> release
 *   Rev 22 fcn.0x038A @ 0x03A4 / @ 0x03EA, identically
 *
 * and republishes the word immediately after each (0x03A2 / 0x03E8).
 *
 * mboxfw never touched this bit. g_mux_state starts at 0xF6 with bit 7 high and
 * nothing lowered it, so whatever the line drives stayed in its
 * not-streaming state permanently. If it gates the analog output -- a mute
 * released only while a stream is up is a completely ordinary design for an
 * interface, and "released while streaming" is exactly the condition stock
 * encodes -- that alone would make mboxfw silent on playback while everything
 * else measured correct. Unverified, because what the line physically drives
 * needs the board; asserting it under stock's condition costs nothing either
 * way and removes the divergence.
 *
 * Stock's single handler covers both directions at once, so "streaming" there
 * means any alt setting selected. mboxfw splits the two directions into
 * separate enable calls, so the equivalent is "either direction running".
 */
static void panel_update_streaming(void)
{
    unsigned char before = g_mux_state;

    if (playback_running || capture_running) {
        g_mux_state &= (unsigned char)~0x80;   /* assert  — Rev 20 @ 0x03A0 */
    } else {
        g_mux_state |= (unsigned char)0x80;    /* release — Rev 20 @ 0x03E6 */
    }
    /* Republish only on a real change. Stock publishes unconditionally, but its
     * two sites are the two transitions; ours is called on every enable call,
     * including the redundant ones a host is free to send. */
    if (g_mux_state != before) {
        mux_write(g_mux_state);                /* Rev 20 @ 0x03A2 / @ 0x03E8 */
    }
}

void streaming_playback_enable(unsigned char on)
{
    playback_running = on ? 1 : 0;
    panel_update_streaming();
    if (on) {
        /* Pull the external chips out of reset if a suspend cleared the
         * bring-up guard. Ports stock's `JB 0x2e,<skip>; LCALL 0x080b` --
         * Rev 20 0x038F/0x0392 on the interface-1 path and 0x0416/0x0419 on
         * interface 2. cs8427_boot_init() carries the guard internally, so
         * this is a no-op on every start after the first. */
        cs8427_boot_init();
        /* Reset the SOF watchdog's shadow so the first frame of a new stream
         * is always evaluated rather than compared against a stale count from
         * the previous stream. */
        sof_bcnt_hi = 0xFF;
        sof_bcnt_lo = 0xFF;
        OEPBBAX2 = EP_BBAX(EP2_OUT_BUF_ADDR);
        OEPBSIZ2 = EP_BSIZE(EP_AUDIO_BUF_SIZE);
        OEPDCNTX2 = 0;
        /* 0xC5 = IEPEN | ISO | BPS field 5 (6 bytes per sample, i.e.
         * stereo 24-bit) per datasheet §6.4.4.6.2. */
        OEPCNF2  = 0xC5;
        /* Rev 20 fcn.0x0398 @ 0x03CD — DMACTL0 |= DMAEN, after the
         * endpoint is enabled. Datasheet §6.5.2.3: "before enabling the
         * DMA channel, all other DMA channel configuration bits must be
         * set to the desired value." */
        DMACTL0 |= DMA_EN;
    } else {
        DMACTL0 &= (unsigned char)~DMA_EN;  /* Rev 20 fcn.0x1013 @ 0x1001 */
        OEPCNF2  = 0;
    }
}

void streaming_capture_enable(unsigned char on)
{
    capture_running = on ? 1 : 0;
    panel_update_streaming();
    if (on) {
        /* Pull the external chips out of reset if a suspend cleared the
         * bring-up guard. Ports stock's `JB 0x2e,<skip>; LCALL 0x080b` --
         * Rev 20 0x038F/0x0392 on the interface-1 path and 0x0416/0x0419 on
         * interface 2. cs8427_boot_init() carries the guard internally, so
         * this is a no-op on every start after the first. */
        cs8427_boot_init();
        IEPBBAX1 = EP_BBAX(EP1_IN_BUF_ADDR);
        IEPBSIZ1 = EP_BSIZE(EP_AUDIO_BUF_SIZE);
        IEPDCNTX1 = 0;
        /* 0xC5 = IEPEN | ISO | BPS field 5 (6 bytes per sample) per
         * datasheet §6.4.4.6.2. Rev 20 fcn.0x0398 @ 0x03C4 */
        IEPCNF1  = 0xC5;
        DMACTL1 |= DMA_EN; /* Rev 20 fcn.0x0398 @ 0x03BD */
    } else {
        DMACTL1 &= (unsigned char)~DMA_EN;  /* Rev 20 fcn.0x0330 @ 0x032D */
        IEPCNF1  = 0;
    }
}

/*
 * SOF service — playback frame-alignment watchdog.
 *
 * This was a no-op, on the reasoning that "Rev 20's Timer 0 ISR at 0x101E is a
 * 9-byte set-a-flag stub, the DMA engine autoruns, so no per-SOF work is
 * needed". Two things wrong with that. Timer 0 is not SOF — SOF is VECINT
 * source 0x14, a different interrupt entirely, so the timer stub said nothing
 * about it. And while Rev 20 genuinely has no SOF handler (its VECINT table
 * entry 20 at 0x0C93+40 points to 0x1034, a bare RET), **Rev 22 does**:
 *
 *   Rev 22 VECINT table 0x0C7D + 20*2 = 0x0CA5 -> 0x0D58
 *
 *   0d58  R6 = DMABCNT0H (0xFFEC)      ; playback buffer content, high byte
 *   0d5d  A  = DMABCNT0L (0xFFEB)      ; ... low byte
 *   0d61  R4 = 0 ; ADD A,#0 ; ADDC     ; widen to 24-bit R4:R6:R7
 *   0d6a  XRL A,0x1C / XRL A,0x1B      ; compare against the saved count
 *   0d71  JZ 0x0D9D                    ; unchanged -> nothing to do
 *   0d73  0x1B = R6 ; 0x1C = R7        ; save the new count
 *   0d77  R5 = 6 ; LCALL 0x0B7F        ; divide by 6, remainder in R5
 *   0d7c  A = R5 ; ORL A,R4 ; JZ 0x0D9D  ; remainder 0 -> aligned, done
 *   0d80  DMACTL0  &= 0x7F             ; ---- resync: stop playback DMA
 *   0d87  OEPDCNTX2 = 0
 *   0d8c  OEPDCNTY2 = 0
 *   0d90  OEPCNF2   = 0xC5             ; re-enable the endpoint
 *   0d96  DMACTL0  |= 0x80             ; restart the DMA
 *
 * `0x0B7F` is a divide-with-remainder helper (8-bit fast path when the
 * dividend fits in R7, long division otherwise; either way R5 holds the
 * remainder on return).
 *
 * What the register is, from the datasheet rather than inference —
 * §6.5.2.4/§6.5.2.5, DMABCNT0L/H: "This register shows the buffer content
 * (bytes) for an ISO OUT endpoint. This register is updated every SOF and is
 * stable for the following USB frame, during which the MCU can read it **to
 * implement USB audio synchronization**." And §2.2.7: "the count in the
 * register represents the number of bytes being transferred from the OUT
 * endpoint buffer to the C-port during the current USB frame... the value of
 * the write pointer address setting minus the read pointer address setting at
 * the time of the USB SOF event."
 *
 * So it is the playback circular buffer's FILL LEVEL, and 6 is one stereo
 * 24-bit sample frame (2 ch x 3 B). Rev 22 is checking whether the buffer
 * holds a whole number of sample frames, and if it does not — meaning the DMA
 * would thereafter emit bytes offset within the frame, splitting samples and
 * swapping channels — it tears the playback path down and restarts it.
 *
 * Why this matters beyond parity: Rev 20 is the firmware documented as needing
 * a v22 flash before playback works, and this watchdog is a playback-only fix
 * that Rev 22 added and Rev 20 lacks. Rev 22 also had to find two IRAM bytes
 * for the saved count, which is why its EP0 pointer moved from 0x1B:0x1C to
 * 0x1D:0x1E — the one otherwise-unexplained low-IRAM difference between the
 * two images (see IRAM_OVERLAY_ANNOTATION.md). mboxfw had Rev 20's behaviour
 * here, i.e. none.
 *
 * DELIBERATE DIVERGENCE: gated on playback_running. Rev 22 runs this on every
 * SOF unconditionally, so a stale misaligned count while playback is stopped
 * would make it write OEPCNF2 = 0xC5 and set DMAEN — enabling playback the
 * host never asked for. Rev 22 gets away with it because the count goes to
 * zero and stops changing, but "gets away with it" is not a reason to copy it.
 */
void streaming_sof(void)
{
    unsigned char hi, lo;
    unsigned int  content;

    if (!playback_running) {
        return;
    }

    /* Read high then low, matching Rev 22's order (0x0D58 then 0x0D5D). The
     * datasheet guarantees the pair is stable for the whole frame after SOF,
     * so there is no tearing window to worry about. */
    hi = DMABCNT0H;
    lo = DMABCNT0L;

    if (hi == sof_bcnt_hi && lo == sof_bcnt_lo) {
        return;                      /* Rev 22 @ 0x0D71 */
    }
    sof_bcnt_hi = hi;                /* Rev 22 @ 0x0D73 */
    sof_bcnt_lo = lo;                /* Rev 22 @ 0x0D75 */

    content = ((unsigned int)hi << 8) | lo;

    /* Derived from the declared format rather than hardcoded, so a channel
     * count or subframe change cannot silently leave the divisor wrong. Equals
     * Rev 22's literal `MOV R5,#0x6` for the format we declare. */
    if (content % (AUDIO_NUM_CHANNELS * AUDIO_SUBFRAME_BYTES) == 0) {
        return;                      /* aligned — Rev 22 @ 0x0D7E */
    }

    tlm_playback_resyncs++;

    DMACTL0 &= (unsigned char)~DMA_EN;  /* Rev 22 fcn.0x0D58 @ 0x0D80 */
    OEPDCNTX2 = 0;                      /* Rev 22 fcn.0x0D58 @ 0x0D87 */
    OEPDCNTY2 = 0;                      /* Rev 22 fcn.0x0D58 @ 0x0D8C */
    OEPCNF2   = 0xC5;                   /* Rev 22 fcn.0x0D58 @ 0x0D90 */
    DMACTL0 |= DMA_EN;                  /* Rev 22 fcn.0x0D58 @ 0x0D96 */
}
