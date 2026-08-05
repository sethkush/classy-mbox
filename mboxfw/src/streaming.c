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
 * RAM[0x1B]:RAM[0x1C]. 0xFF/0xFF is an impossible real count for a 640-byte
 * buffer (#162 -- and it was equally impossible for the 512-byte one this
 * comment used to name), so it doubles as "no reading yet" and forces the
 * first frame of a
 * stream to be evaluated. See streaming_sof(). */
static __data unsigned char sof_bcnt_hi = 0xFF;
static __data unsigned char sof_bcnt_lo = 0xFF;

/* Currently-active sample rate — mirrors g_sample_rate in usb.c. */
static __data unsigned long stream_rate = 48000UL;

/* Stock's RAM[0x08]. See streaming.h for the numbering and the citations.
 * Seeded to 3 because hw_init leaves the part on internal 48 kHz, which is the
 * mode stock's boot path also lands in — and NOT on mode 1, deliberately: see
 * the boot-default note in streaming_set_rate(). */
__data unsigned char g_clock_mode = 3;

/* Last value written to the C-port serial-clock dividers (CPTCNF4/CPTRXCNF4),
 * seeded to hw_init's boot value so a rate request that does not change it
 * writes nothing. See streaming_set_rate(). */
static __data unsigned char cport_div = 0x03;

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
    /* #46 — 88.2 and 96 kHz reuse the 44.1 and 48 kHz frequency words and
     * halve the C-port serial-clock divider instead. Nothing else differs.
     *
     * WHY NOT A BIGGER SYNTH WORD. Datasheet §2.2.6.1 gives the synthesizer
     * output as 600/N MHz for 24 <= N < 50, i.e. a range of 12-25 MHz, and
     * §2.2.6 states the same range for the block. Stock's 48 kHz word
     * 0x61A80F is N = 24.4140625 -> 24.576 MHz, which is the datasheet's own
     * worked example, and 24.576 MHz is 512 x 48000. Keeping that ratio at 96
     * kHz would need 49.152 MHz, twice the top of the synthesizer's range.
     * There is no 96 kHz frequency word; the part cannot make one.
     *
     * WHAT IS AVAILABLE. Per Figure 2-1 the C-port serial clock is
     * CSCLK = MCLKO / B with B from CPTCNF4[2:0] (playback) and
     * CSCLK2 = MCLKO2 / B2 with B2 from CPTRXCNF4[2:0] (capture), encoded
     * 001b = /2, 010b = /3, 011b = /4 (§6.5.4.13). The frame is a fixed 128
     * bits, so at MCLKO = 24.576 MHz, B = 4 gives 6.144 MHz / 128 = 48 kHz and
     * B = 2 gives 12.288 MHz / 128 = 96 kHz. The codec then sees MCLK = 256fs
     * rather than 512fs, which is an ordinary ratio for a converter running at
     * 96 kHz.
     *
     * THIS IS MEASURED, not projected. When CPTRXCNF4 was wrongly left at 0x01
     * between 2026-07-26 and 2026-07-28, hardware read a steady IEPDCNTX1 of
     * 96 samples per USB frame where stock delivers 48, and 88 where stock
     * delivers 44 — both exactly 2x, from this divider alone. See the long
     * comment on CPTRXCNF4 in hw_init.c. That accident is the whole mechanism
     * of this feature; all that was missing was doing it on purpose, to both
     * dividers, with the frequency word left alone.
     *
     * WHAT IS NOT ESTABLISHED: whether the CONVERTERS follow. The codec has no
     * register interface — its entire control surface is the 16-bit word in
     * codec.c (mute pair, reset, mono, source), with no rate or mode field —
     * so it must be deriving its rate from MCLK and the frame clock, and
     * nothing in the firmware can tell us whether its modulators are specified
     * to 96 kHz. That is a bench question, and until it is answered these
     * rates are reachable ONLY through the vendor request (TLM_REQ_SET_CLOCK
     * wValue 3/4). They are deliberately NOT in the descriptors and the class
     * SET_CUR path still stalls them, so no host can select a rate whose
     * analog behaviour has not been measured.
     *
     * NOVEL — reason: stock has no 88.2/96 kHz path. Its only /2 write is the
     * mode-5 branch (Rev 20 0x07A0, Rev 22 0x077E), which sets CPTRXCNF4 alone
     * and is unreachable — nothing posts work code 0x0A (see
     * FINDING_codec_word_bits_resolved.md). The CPTEN bracket below is that
     * branch's ordering; the playback-side divider write has no stock
     * precedent at all. */
    unsigned char dbl = (unsigned char)(hz > 50000UL);

    stream_rate = hz;
    if (dbl) {
        hz >>= 1;   /* 88200 -> 44100, 96000 -> 48000: from here down the
                     * frequency word, the clock mode and the channel-status
                     * base are those of the half-rate, unchanged. */
    }
    if (hz != 0UL) {
        /* Stock's mode-5 ordering, extended to the playback divider:
         * CPTEN off, program, CPTEN on, clear VECINT.
         *   0799  GLOBCTL &= 0xFE
         *   07a0  CPTRXCNF4 = 0x01
         *   07a6  GLOBCTL |= 0x01
         *   07af  VECINT = 0
         * The codec port is stopped across the divider change because the
         * divider feeds the clock the port is actively framing with.
         *
         * BOTH DIRECTIONS OF THE CHANGE ARE HANDLED. Restoring /4 matters as
         * much as setting /2: a device that had been to 96 kHz and was then
         * asked for 48 kHz would otherwise stay at 256fs and play back an
         * octave high, with no register left disagreeing to find it by. The
         * /4 values are hw_init's, cited there.
         *
         * hz == 0 (slaved to S/PDIF) skips this entirely: the incoming stream
         * decides the rate, and stock's mode 1 touches no C-port register.
         *
         * The bracket fires only when the value actually CHANGES. Stock's
         * modes 2 and 3 touch no C-port register at all, so writing the /4
         * value on every
         * SET_CUR(48000) would put a CPTEN stop/start into the ordinary rate
         * path — on a live stream, on a part whose USB engine is already up
         * (#47). The common path stays byte-identical to what 0x0023 ran; the
         * bracket fires only on a genuine divider change. `cport_div` is
         * seeded to hw_init's boot value, so the first 48 kHz request after a
         * cold boot writes nothing. */
        unsigned char b = dbl ? 0x01 : 0x03;
        if (b != cport_div) {
        cport_div = b;
        GLOBCTL  &= 0xFE;   /* Rev 20 fcn.0x0728 @ 0x0799, Rev 22 @ 0x0777 */
        CPTCNF4   = b;      /* /4 Rev 20 fcn.0x08CB @ 0x0911, Rev 22 fcn.0x07EC @ 0x0832.
                             * /2 is NOVEL — reason: playback-side doubling.
                             * Stock's mode 5 halves only the receive divider. */
        CPTRXCNF4 = b;      /* /4 Rev 20 fcn.0x08CB @ 0x0929, Rev 22 fcn.0x07EC @ 0x084A;
                             * /2 Rev 20 fcn.0x0728 @ 0x07A0, Rev 22 @ 0x077E */
        GLOBCTL  |= 0x01;   /* Rev 20 fcn.0x0728 @ 0x07A6, Rev 22 @ 0x0784 */
        /* STOCK'S NEXT WRITE IS DELIBERATELY NOT COPIED. Both images follow
         * the CPTEN re-enable with VECINT = 0 — Rev 20 stores at 0x07B1, Rev
         * 22 at 0x078F, reached by INC DPTR off the GLOBCTL write the helper
         * at 0x0DEB / 0x0EC7 just performed, which is why a listing that stops
         * at the LCALL puts it two bytes earlier.
         *
         * It is omitted for the reason CLAUDE.md gives for not mirroring a
         * write on "stock does it" alone. Two things are true here that are
         * not true of the rest of this bracket:
         *
         *   - The branch it comes from has NEVER RUN. Mode 5 is reachable only
         *     from work code 0x0A, and nothing in either image posts 0x0A (see
         *     FINDING_codec_word_bits_resolved.md). This is the one citation
         *     in this file whose behaviour no hardware has ever exercised.
         *   - VECINT is how usb_service() learns which USB event fired (see
         *     isr.c). streaming_set_rate() runs out of a control-request
         *     handler, on a LIVE USB engine that mboxfw brings up before
         *     hw_init (#47) — precisely the ordering difference CLAUDE.md
         *     warns about. Clearing the vector register there can discard a
         *     pending endpoint event that stock, servicing USB later, would
         *     never have had in flight.
         *
         * The divider change does not need it: CPTEN off/on is what makes the
         * write safe, and that half is copied. */
        }
    }

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
     *
     * hz == 0 IS CLOCK MODE 1 — slaved to the incoming S/PDIF stream (#177).
     * It is not a sentinel this firmware invented: Rev 20's SET_CUR data
     * handler `ep0_out_data_handler` @0x0D25 tests the rate's low byte and
     * posts work code 0x06 when it is ZERO (0x0D40-0x0D42), and cmd6
     * @0x0478 is `MOV R7,#1; LCALL 0x0728` — mode 1, nothing else. The
     * kernel quirk drives the same encoding from the other side
     * (`snd_mbox1_set_clk_source`, rate 0 = slave to S/PDIF), and
     * `setup_get_sample_freq` @0x008A reports 0,0,0 back whenever
     * RAM[0x08] == 1. Three artifacts, one encoding.
     *
     * No frequency word is programmed in this mode, because nothing is
     * being synthesized — both master clocks come from MCLKI instead.
     *
     * MODE 1 MUST NEVER BE THE BOOT DEFAULT, and this is a safety property
     * rather than a preference. `ACGCTL = 0x0D` sources both codec master
     * clocks from MCLKI, which is only useful if MCLKI is wired to the
     * CS8427's RMCK. cs8427_boot_init() writes CONTROL1 = 0x01 with SWCLK = 0
     * so RMCK carries the recovered clock, and stock's mode 1 is coherent only
     * under that wiring — but no schematic has been read, so it stays an
     * inference. If it is wrong, mode 1 leaves the codec with no clock at all.
     *
     * What makes that survivable is that the CPU runs from the oscillator, not
     * from MCLKO: a wrong guess costs audio and nothing else, EP0 keeps
     * answering, and another vendor request undoes it. No power cycle, no 2 km
     * round trip. That holds ONLY while every path into this arm is
     * host-initiated — hw_init leaves the part on mode 3, g_clock_mode is
     * seeded to 3, and no boot path calls this with 0.
     *
     * RESOLVED 2026-08-04, build 0x0020 on hardware: the inference was right.
     * With the CS8427 as serial slave (SOMS = 0, so OSCLK/OLRCK are TAS
     * outputs derived from MCLKO), a dead MCLKI in this mode would stop the
     * C-port and the DMA would emit zero-length packets. Capture instead ran
     * 576000 frames in 12.00 s with bit-stable content across the switch —
     * S/PDIF in at a bit-exact -9.0 dBFS. See FINDING_spdif_input_works.md.
     * The boot-default rule stays: it costs nothing and it is what made the
     * question safe to ask remotely in the first place.
     */
    if (hz == 0UL) {
        /* ACGCTL = 0x0D: MCLKO1S = 01, DIVEN, MCLKO2S = 01 — BOTH codec
         * master clocks sourced from MCLKI, the external clock input
         * (datasheet §6.5.3.11). Paired with CLOCKSOURCE = 0x41 in the
         * tail below; the two writes are meaningless apart. */
        ACGCTL = 0x0D;    /* Rev 20 fcn.0x0728 @ 0x074D — mode 1 */
        g_clock_mode = 1; /* Rev 20 fcn.0x0728 @ 0x0753 — MOV 0x08,#1 */

    } else if (hz == 48000UL) {
        ACG1FRQ1 = 0xA8;  /* Rev 20 fcn.0x0DEC @ 0x0DEC — mode 3, 48 kHz */
        ACG1FRQ2 = 0x61;  /* Rev 20 fcn.0x0DEC @ 0x0DF2 */
        ACG1FRQ0 = 0x0F;  /* Rev 20 fcn.0x0DEC @ 0x0DF8 */
        ACG2FRQ1 = 0xA8;  /* Rev 20 fcn.0x0DEC @ 0x0DFE */
        ACG2FRQ2 = 0x61;  /* Rev 20 fcn.0x0DEC @ 0x0E04 */
        ACG2FRQ0 = 0x0F;  /* Rev 20 fcn.0x0DEC @ 0x0E0A */
        g_clock_mode = 3; /* Rev 20 fcn.0x0728 @ 0x0791 — MOV 0x08,#3 */

    } else {
        ACG1FRQ2 = 0x6A;  /* Rev 20 fcn.0x0728 @ 0x0765 — mode 2, 44.1 kHz */
        ACG1FRQ1 = 0x4B;  /* Rev 20 fcn.0x0728 @ 0x075F */
        ACG1FRQ0 = 0x20;  /* Rev 20 fcn.0x0728 @ 0x076B */
        ACG2FRQ2 = 0x6A;  /* Rev 20 fcn.0x0728 @ 0x0777 */
        ACG2FRQ1 = 0x4B;  /* Rev 20 fcn.0x0728 @ 0x0771 */
        ACG2FRQ0 = 0x20;  /* Rev 20 fcn.0x0728 @ 0x077D */
        g_clock_mode = 2; /* Rev 20 fcn.0x0728 @ 0x0785 — MOV 0x08,#2 */

    }
    /* #46 — NOVEL: modes 6 and 7 are 88.2 and 96 kHz, i.e. modes 2 and 3 with
     * the C-port dividers halved. Stock numbers 1..5 and posts none above 5,
     * so 6/7 extend the space rather than colliding with it. Telemetry block 9
     * byte 7 reports this, which is how a bench run tells a doubled rate from
     * its base rate without inferring it from what was requested. */
    if (dbl) {
        g_clock_mode += 4;
    }
    if (hz != 0UL) {
        /* Shared by both rates: Rev 20's modes 2 and 3 both end at the tail
         * 0x0E0F..0x0E16, which writes ACGCTL = 0x06 (DIVEN + both MCLKO
         * sourced from their synthesizers after ÷M). Mode 1 does NOT reach
         * this tail — it writes 0x0D and jumps straight to 0x07C5. */
        ACGCTL = 0x06;        /* Rev 20 @ 0x0E10 */
    }

    /* CS8427 CLOCKSOURCE, the other half of the mode.
     *
     * Stock queues the (register, value) pair in RAM[0x31]/[0x32] inside each
     * mode arm and the shared tail issues it once, at 0x07C5-0x07C9:
     *
     *   mode 1  @0x0756-0x0759   0x31 = 0x04, 0x32 = 0x41
     *   mode 2  @0x0788          LCALL 0x0E20 -> 0x31 = 0x04, 0x32 = 0x40
     *   mode 3  @0x0794          LCALL 0x0E20 -> same
     *
     * 0x40 = RUN | RXD=00 (CS8427_RXDILRCK): the receiver PLL follows the
     * TAS-driven word clock. 0x41 = RUN | RXD=01 (CS8427_RXDAES3INPUT): it
     * recovers the clock from the incoming AES3 stream instead. One bit is
     * the entire internal-vs-slaved distinction.
     *
     * mboxfw wrote neither until #177 — the boot init left CLOCKSOURCE at
     * 0x40 from cs8427_boot_init() and no rate change ever revisited it,
     * which happens to be right for modes 2/3 and is why the omission never
     * showed up. It is not right for mode 1. */
    cs8427_write(0x04, (hz == 0UL) ? 0x41 : 0x40);  /* Rev 20 fcn.0x0728 @ 0x07C9 */

    /* Common tail — same for both rates. Rev 20 fcn.0x0728 0x07C4-0x07FF.
     *
     * Rev 20 clears the four BYTE-COUNT registers (X and Y buffer counts
     * for both streaming endpoints) and nothing else. It does NOT touch
     * IEPBSIZ1/OEPBSIZ2 — those are the buffer SIZE registers, and as of
     * #163 they are written in usb_ep0_setup() and nowhere else, which is
     * where stock writes them.
     *
     * This comment previously ended with "Rev 20 sets BBAX/BSIZ exactly once
     * at 0x09B1-0x09C6 and never again" while stating in the same breath that
     * mboxfw programmed them "once in streaming_capture_enable() /
     * streaming_playback_enable()". Those are not the same "once": stock's is
     * once per boot, mboxfw's was once per SET_INTERFACE(alt=1). The
     * divergence was written down here, correctly, and read as agreement.
     *
     * This code used to zero the SIZE registers instead of the Y counts,
     * which left whichever endpoint was already streaming with a
     * zero-length buffer if a SET_CUR(sample rate) arrived after
     * SET_INTERFACE. */
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
     * is established from the bytes.
     *
     * #171 SETTLED 2026-08-04: the pair gates BOTH directions, measured
     * across two units -- 71 dB on the output side, 0 of 95232 non-zero
     * samples on the input side (FINDING_bringup_waveform.md). The
     * MBOX_NO_MUTE_PAIR switch that isolated it is gone, exactly as
     * MBOX_PLAYBACK_BYOR went once a loopback settled #161. What replaces it
     * for the still-open per-direction question is CODEC23_MUTE_PAIR. */
    /* #46: the mask is a build-time constant, so a one-bit variant costs
     * nothing at runtime. See MBOX_MUTE_PAIR_MASK in the Makefile — the open
     * question is whether these are one gate or two, which is exactly what
     * decides whether a UAC Feature Unit can carry an honest Mute. */
#if CODEC23_MUTE_PAIR != 0
    g_codec_state_23 |= (unsigned char)CODEC23_MUTE_PAIR;
#endif
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

    /* CHANNEL STATUS — what the S/PDIF transmitter declares downstream.
     *
     * Stock does this OUTSIDE the clock routine, in the two rate work codes,
     * each branching on the Selector Unit bit 0x25.4 (`JNB 0x2c`):
     *
     *   cmd7 (44.1k) @0x0480     cmd8 (48k) @0x049A
     *     0x0485 JNB 0x2c          0x049F JNB 0x2c
     *     S/PDIF -> LCALL 0x0568   S/PDIF -> LCALL 0x0568
     *                reg 0x04 = 0x41 ; reg 0x12 = 0x00
     *     analog -> reg 0x23 = 0x00  analog -> reg 0x23 = 0x40
     *               then LCALL 0x0582 -> reg 0x24 = 0x80
     *
     * CORU_DATABUF starts at register 0x20, so reg 0x20+n is channel-status
     * byte n: reg 0x23 = byte 3 = sampling frequency (0x00 = 44.1 kHz,
     * 0x40 = 48 kHz, MSB-aligned nibble, IEC 60958 consumer) and reg 0x24 =
     * byte 4 = word length. Register names from
     * reference/cs8427/alsa_cs8427.h.
     *
     * The polarity is the point: when the device is INTERNALLY clocked it is
     * the master and must declare its rate; when it is slaved it declares
     * nothing and re-asserts the recovery source instead. Mode 1 skips this
     * block entirely — stock's cmd6 (rate 0) is `MOV R7,#1; LCALL 0x0728`
     * and no channel-status write at all. */
    if (hz != 0UL) {
        if (g_codec_state_25 & CODEC25_SEL_SPDIF) {
            cs8427_write(0x04, 0x41);   /* Rev 20 fcn.0x0568 @ 0x0572 */
            cs8427_write(0x12, 0x00);   /* Rev 20 fcn.0x0568 @ 0x057F */
        } else {
            /* Rev 20 cmd7 @0x048E / cmd8 @0x04A8, issued by fcn.0x0582 */
            /* #46 adds the doubled-rate codes. IEC 60958 consumer channel
             * status byte 3 holds the sampling frequency in bits 24-27, and
             * the CS8427 presents that field in the register's HIGH nibble,
             * bit 24 first — so the register value is the bit-reverse of
             * ALSA's 4-bit code (include/uapi/sound/asound.h,
             * IEC958_AES3_CON_FS_*):
             *
             *   44.1 kHz  code 0x0 = 0000b -> 0000b -> 0x00   (stock)
             *   48   kHz  code 0x2 = 0010b -> 0100b -> 0x40   (stock)
             *   88.2 kHz  code 0x8 = 1000b -> 0001b -> 0x10
             *   96   kHz  code 0xA = 1010b -> 0101b -> 0x50
             *
             * The two stock values are what fixes the convention: 48 kHz is
             * the only one of the four whose code has a single set bit in a
             * position that distinguishes reversed from straight, and stock
             * writes 0x40, not 0x20. The doubled bit is the same 0x10 in both
             * new codes, which is why this is an OR rather than a table. */
            cs8427_write(0x23, ((hz == 48000UL) ? 0x40 : 0x00)
                             | (dbl ? 0x10 : 0x00));
            cs8427_write(0x24, 0x80);   /* Rev 20 fcn.0x0582 @ 0x0593 */
        }
    }
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
        /* #163: base and size are NOT re-declared here. usb_ep0_setup() sets
         * them once, as stock does -- the DMA and UBM pointers into this
         * buffer are read-only, so re-basing under them cannot reset them.
         * Stock's stream-start path touches only xEPCNF and DMAEN, and this
         * path now matches it. */
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
        /* #163: see the note in streaming_playback_enable() -- base and size
         * belong to usb_ep0_setup(), written once. */
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
    /* Subtract rather than use `%`. The divisor is 6, not a power of two, so
     * `content % 6` made SDCC link the generic 16-bit modulo routine
     * (_moduint, 77 bytes) for this one test. content is a DMA byte count
     * bounded by the endpoint buffer, so repeated subtraction terminates
     * quickly and costs a fraction of that. Same result, same citation. */
    {
        unsigned int rem = content;
        while (rem >= (AUDIO_NUM_CHANNELS * AUDIO_SUBFRAME_BYTES) * 8u)
            rem -= (AUDIO_NUM_CHANNELS * AUDIO_SUBFRAME_BYTES) * 8u;
        while (rem >= (AUDIO_NUM_CHANNELS * AUDIO_SUBFRAME_BYTES))
            rem -= (AUDIO_NUM_CHANNELS * AUDIO_SUBFRAME_BYTES);
        if (rem == 0) {
            return;                  /* aligned — Rev 22 @ 0x0D7E */
        }
    }


    /* Counted, not silent. This watchdog firing occasionally is normal
     * operation; firing every frame means playback is being restarted faster
     * than it can produce sound, and the two are indistinguishable from the
     * host — both present as a stream that runs without error and emits
     * nothing, which is exactly what 96 kHz playback did on 0x002A. Block 3
     * byte 5. Saturating at 0xFF so a thrash pins the counter rather than
     * wrapping to a small value that reads as healthy. */
    TLM_INC8(tlm_playback_resyncs);

    DMACTL0 &= (unsigned char)~DMA_EN;  /* Rev 22 fcn.0x0D58 @ 0x0D80 */
    OEPDCNTX2 = 0;                      /* Rev 22 fcn.0x0D58 @ 0x0D87 */
    OEPDCNTY2 = 0;                      /* Rev 22 fcn.0x0D58 @ 0x0D8C */
    OEPCNF2   = 0xC5;                   /* Rev 22 fcn.0x0D58 @ 0x0D90 */
    DMACTL0 |= DMA_EN;                  /* Rev 22 fcn.0x0D58 @ 0x0D96 */
}
