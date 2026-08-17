/*
 * USB Audio Class 1.0 descriptor set for Mbox 1.
 *
 * Layout:
 *   Interface 0     — AudioControl
 *      IT USB-OUT (host→device)  → OT Line-Out           (playback path)
 *      IT Analog-In               → OT USB-IN (device→host) (capture path)
 *   Interface 1     — AudioStreaming (playback), 2 alt settings
 *      alt 0 = zero-bandwidth (no endpoint)
 *      alt 1 = active: EP2 OUT adaptive iso
 *   Interface 2     — AudioStreaming (capture), 2 alt settings
 *      alt 0 = zero-bandwidth
 *      alt 1 = active: EP1 IN adaptive iso
 *
 * 2 channels × 24-bit × {44.1, 48} kHz.
 */

#include "usb.h"

/* --- Device descriptor ------------------------------------------------ */
const unsigned char __code AppDevDesc[APP_DEV_DESC_LEN] = {
    18,                     /* bLength */
    USB_DT_DEVICE,          /* bDescriptorType */
    0x10, 0x01,             /* bcdUSB = 1.10 */
    0x00,                   /* bDeviceClass — deferred to interface */
    0x00,                   /* bDeviceSubClass */
    0x00,                   /* bDeviceProtocol */
    8,                      /* bMaxPacketSize0 = 8 (matches Rev 20) */
    MBOX_VID & 0xFF, (MBOX_VID >> 8) & 0xFF,
    MBOX_PID & 0xFF, (MBOX_PID >> 8) & 0xFF,
    MBOX_BCD_DEVICE & 0xFF, (MBOX_BCD_DEVICE >> 8) & 0xFF,
    0x01,                   /* iManufacturer  = string #1 */
    0x02,                   /* iProduct       = string #2 */
    APP_ISERIAL,            /* iSerialNumber — 3 if MBOX_UNIT=A/B, else 0 (usb.h) */
    0x01                    /* bNumConfigurations */
};

#ifdef MBOX_SERIAL_EEPROM
/* #226: the SAME device descriptor with iSerialNumber = 3 instead of 0.
 *
 * WHY A SECOND __code COPY AND NOT A PATCHED RAM COPY. serialno.c used to hold
 * an 18-byte RAM duplicate for exactly this one byte. Every persistent byte
 * there is now internal RAM (XDATA does not exist on this board -- see the #226
 * note in eeprom.c), and internal RAM is shared with the stack: holding this
 * descriptor cost 18 bytes of stack, which fell to 55 and is not a margin worth
 * having on a part whose stack corruption would show up as an intermittent hang
 * 2 km away. CODE space is the cheap resource here, so the variant lives there.
 *
 * The index and the string are still decided together from g_serial_ok, which
 * is the property serialno.h insists on: a descriptor advertising string 3 when
 * no string 3 exists makes some hosts abandon enumeration. */
const unsigned char __code AppDevDescSN[APP_DEV_DESC_LEN] = {
    18,                     /* bLength */
    USB_DT_DEVICE,          /* bDescriptorType */
    0x10, 0x01,             /* bcdUSB = 1.10 */
    0x00,                   /* bDeviceClass — deferred to interface */
    0x00,                   /* bDeviceSubClass */
    0x00,                   /* bDeviceProtocol */
    8,                      /* bMaxPacketSize0 = 8 (matches Rev 20) */
    MBOX_VID & 0xFF, (MBOX_VID >> 8) & 0xFF,
    MBOX_PID & 0xFF, (MBOX_PID >> 8) & 0xFF,
    MBOX_BCD_DEVICE & 0xFF, (MBOX_BCD_DEVICE >> 8) & 0xFF,
    0x01,                   /* iManufacturer  = string #1 */
    0x02,                   /* iProduct       = string #2 */
    3,                      /* iSerialNumber — ALWAYS 3: this variant is
                             * served only when g_serial_ok, i.e. only when
                             * string 3 exists. NOT APP_ISERIAL, which is the
                             * compile-time MBOX_UNIT= path and is 0 here. */
    0x01                    /* bNumConfigurations */
};
#endif

/* --- Configuration descriptor bundle --------------------------------- */
/*
 * A single "configuration" holds every interface and its endpoints, plus
 * every class-specific descriptor. Layout below is in the order the USB
 * host will parse it.
 */

/* Total length = sum of every descriptor below. Defined in usb.h so
 * usb.c's stage_reply() call uses the same constant — it previously
 * derived the length by reading AppConfigDesc[2..3] at runtime, which is
 * the pattern that broke string descriptors on hardware. */
#define CFG_TOTAL_LEN   APP_CFG_TOTAL_LEN

const unsigned char __code AppConfigDesc[CFG_TOTAL_LEN] = {

    /* ---- Standard Configuration Descriptor (9 bytes) ---- */
    9, USB_DT_CONFIG,
    CFG_TOTAL_LEN & 0xFF, (CFG_TOTAL_LEN >> 8) & 0xFF,
    3,                      /* bNumInterfaces = AC + AS-out + AS-in */
    1,                      /* bConfigurationValue */
    0,                      /* iConfiguration */
    USB_CFG_BUS_POWERED,    /* bmAttributes */
    240,   /* bMaxPower 240 = 480 mA — matches stock Rev 20 exactly
            * (config descriptor at rev20 image 0x05A8). We asked for 250
            * (500 mA); stock enumerates on this hub at 240, so align and
            * remove the variable. */                    /* bMaxPower × 2mA = 500 mA (matches Rev 20) */

    /* ---- Interface 0: AudioControl (9 bytes) ---- */
    9, USB_DT_INTERFACE,
    0,                      /* bInterfaceNumber */
    0,                      /* bAlternateSetting */
    0,                      /* bNumEndpoints — #228 retired the status EP */
    0x01, UAC_SUBCLASS_CONTROL, 0x00,
    0,                      /* iInterface */

    /* ---- Class-specific AC interface header (10 bytes with 2 AS IF refs) ---- */
    10, USB_DT_CS_INTERFACE, UAC_AC_HEADER,
    0x00, 0x01,             /* bcdADC = 1.0 */
    AC_BLOCK_LEN & 0xFF,    /* wTotalLength of the class-specific AC block.
                             * Computed in usb.h from the per-descriptor
                             * lengths rather than spelled out again here --
                             * this field and the array contents are the same
                             * fact twice, and a host that reads a short
                             * wTotalLength silently ignores every unit past
                             * it, which presents as "the selector does not
                             * exist" rather than as a descriptor error. */
    (AC_BLOCK_LEN >> 8) & 0xFF,
    2,                      /* bInCollection = 2 streaming interfaces */
    1,                      /* baInterfaceNr(0) = AS-playback */
    2,                      /* baInterfaceNr(1) = AS-capture */

    /* ---- Input Terminal: host USB-out stream (12 bytes) ---- */
    12, USB_DT_CS_INTERFACE, UAC_AC_INPUT_TERMINAL,
    TERM_USB_OUT_STREAM,
    UAC_TT_USB_STREAMING & 0xFF, (UAC_TT_USB_STREAMING >> 8) & 0xFF,
    0,                      /* bAssocTerminal */
    2,                      /* bNrChannels */
    0x03, 0x00,             /* wChannelConfig = FL + FR */
    0, 0,                   /* iChannel, iTerminal */

    /* ---- Input Terminal: analog line-in (12 bytes) ----
     *
     * A single stereo terminal. Between 2026-08-03 and the same day this was
     * six mono terminals feeding two Selector Units and a fixed Mixer Unit, so
     * a host could pick mic/line/inst per channel (#159). It worked on both
     * hosts and was removed deliberately -- see
     * FINDING_macos_one_input_selector.md and the branch
     * feature/uac-selector-units, which holds the whole implementation.
     *
     * Why it went: macOS can only ever surface ONE input-source control (its
     * driver creates every input selector as kIOAudioControlChannelIDAll, and
     * a second one would need a second engine, which needs a second capture
     * DMA channel the TAS1020B does not have). So the host-side control was
     * per-channel on Linux and single-channel on macOS, while the front-panel
     * buttons already do both channels on both hosts. Carrying ~350 bytes of
     * firmware for an inconsistent convenience was the wrong trade on a part
     * with 6016 bytes of program RAM. */
    12, USB_DT_CS_INTERFACE, UAC_AC_INPUT_TERMINAL,
    TERM_ANALOG_IN,
    UAC_TT_ANALOG_IN & 0xFF, (UAC_TT_ANALOG_IN >> 8) & 0xFF,
    0,                      /* bAssocTerminal */
    2,                      /* bNrChannels */
    0x03, 0x00,             /* wChannelConfig = FL + FR */
    0, STR_ANALOG_IN_IDX,          /* iChannel, iTerminal (#204) */

    /* ---- Input Terminal: S/PDIF receiver (12 bytes) ---- #160
     *
     * The CS8427's AES3 receiver, routed to the codec port by the Selector
     * Unit below. Stereo, because the receiver is stereo (CONTROL2 = 0x20
     * leaves both receiver and transmitter in stereo).
     *
     * 0x0605 is "S/PDIF interface" from the UAC1 Terminal Types document
     * §2.4 (External Terminals) — the same value stock declares. */
    12, USB_DT_CS_INTERFACE, UAC_AC_INPUT_TERMINAL,
    TERM_SPDIF_IN,
    UAC_TT_SPDIF & 0xFF, (UAC_TT_SPDIF >> 8) & 0xFF,
    0,                      /* bAssocTerminal */
    2,                      /* bNrChannels */
    0x03, 0x00,             /* wChannelConfig = FL + FR */
    0, STR_SPDIF_IN_IDX,          /* iChannel, iTerminal (#204) */

    /* ---- Selector Unit: analog vs S/PDIF (8 bytes) ---- #160, #228
     *
     * bLength = 6 + bNrInPins per UAC1 §4.3.2.4. Pin ORDER IS THE PROTOCOL:
     * SET_CUR/GET_CUR carry a 1-based index into baSourceID, so pin 1 = analog
     * and pin 2 = S/PDIF -- the encoding usb.c implements and the kernel quirk
     * documents ("ANALOG Source -> 0x01, S/PDIF Source -> 0x02").
     *
     * #228 TOOK THIS BACK TO TWO PINS, and that is a correction rather than a
     * retreat. #203 added instrument as position 3 and #224 added microphone as
     * position 4, which made this one control do two unrelated jobs:
     *
     *   - S/PDIF vs analog is GLOBAL. One bit, 0x25.4, swings the whole capture
     *     stream between the CS8427 and the ADC. There is no front-panel button
     *     for it, so this control is the ONLY way to reach the S/PDIF input.
     *   - mic / line / instrument is PER CHANNEL, set by the 74HC157 muxes from
     *     the front-panel buttons, independently for each channel.
     *
     * A UAC1 Selector has one output and SET_CUR carries one index, so the
     * four-position form could not express the per-channel state at all -- and
     * selecting an analog position from a host FORCED BOTH CHANNELS to the same
     * front end, silently destroying a setting made physically at the unit.
     * Two positions describe exactly what the hardware makes global, and leave
     * the per-channel choice with the only thing that can express it.
     *
     * This also removes the reason macOS's single-selector limit hurt: one
     * selector is now the right number, not a compromise
     * (FINDING_macos_one_input_selector.md). And nothing on the panel changes a
     * host-visible control any more, so the stale Core Audio reading of #227
     * has nothing left to go stale.
     *
     * ONLY ONE INPUT IS EVER LIVE. That property is unchanged and is the
     * Selector's doing: N pins, one output, one index. "Line and S/PDIF
     * together" is not expressible and no host can ask for it. The construct
     * that WOULD allow simultaneous inputs is a Mixer Unit, which this firmware
     * deliberately does not declare -- and the hardware could not honour one:
     * 0x25.4 swaps the stream, it does not blend.
     *
     * The instrument and microphone INPUT TERMINALS went with the pins. They
     * were declared (#203, #224) because each had been measured -- the XLR path
     * carries audio, 69.9 dB at 1234 Hz -- and that measurement still stands;
     * what changed is that the host no longer chooses between them, so a
     * terminal per front end would be topology the host cannot act on. The
     * remaining analog terminal is typed 0x0601, ANALOG CONNECTOR, rather than
     * 0x0603 LINE: the panel decides which connector is live, and claiming
     * "line" would be the same lie #225 fixed. */
    8, USB_DT_CS_INTERFACE, UAC_AC_SELECTOR_UNIT,
    UNIT_SELECTOR,
    2,                      /* bNrInPins */
    TERM_ANALOG_IN,         /* baSourceID(1) — position 1 = ANALOG (unchanged) */
    TERM_SPDIF_IN,          /* baSourceID(2) — position 2 = S/PDIF (unchanged) */
    0,                      /* iSelector */

    /* ---- Feature Unit: playback Mute (10 bytes) ---- #190
     *
     * Sits between the USB-out stream and the two output terminals, so a host
     * walking back from either one finds it. bSourceID chains from the stream;
     * both Output Terminals below now source from THIS unit rather than from
     * the stream directly. A Feature Unit nothing routes through is a Feature
     * Unit the host never finds, however correct its own descriptor is --
     * exactly the trap #160 recorded for the Selector Unit.
     *
     * MASTER CHANNEL ONLY. bmaControls(0) carries Mute; the two per-channel
     * entries are zero. That is not a simplification, it is the hardware:
     * 0x23.3 is one bit gating the whole playback path, so a per-channel mute
     * would be two controls moving one gate -- each silently changing the
     * other, which is worse than not offering them.
     *
     * NO VOLUME. The codec has no gain field anywhere in its 16-bit word
     * (FINDING_codec_word_bits_resolved.md), the front-panel gain is analog
     * pots, and the samples never pass through the 8051 -- the DMA moves them
     * between the endpoint buffer and the codec port with no CPU involvement.
     * Volume is absent, not omitted. */
    FU_DESC_LEN, USB_DT_CS_INTERFACE, UAC_AC_FEATURE_UNIT,
    UNIT_FU_PLAYBACK,
    TERM_USB_OUT_STREAM,   /* bSourceID = the playback stream */
    1,                     /* bControlSize */
    UAC_FU_CTRL_MUTE,      /* bmaControls(0) master — Mute */
    0,                     /* bmaControls(1) ch1 */
    0,                     /* bmaControls(2) ch2 */
    0,                     /* iFeature */

    /* ---- Output Terminal: analog line-out (9 bytes) ---- */
    9, USB_DT_CS_INTERFACE, UAC_AC_OUTPUT_TERMINAL,
    TERM_LINE_OUT,
    UAC_TT_LINE_OUT & 0xFF, (UAC_TT_LINE_OUT >> 8) & 0xFF,
    0,
    UNIT_FU_PLAYBACK,      /* #190 bSourceID = playback Feature Unit */
    STR_LINE_OUT_IDX,      /* iTerminal (#204) */

    /* ---- Output Terminal: S/PDIF transmitter (9 bytes) ---- #187
     *
     * The AES3 transmitter has been running since the first build and no host
     * could see it. It is fed from the same place the line out is — the
     * playback side of the C-port — so both terminals take bSourceID =
     * TERM_USB_OUT_STREAM. That is two Output Terminals on one source, which
     * is exactly what UAC1 §3.2 describes for a signal split to two physical
     * outputs, and it is what the hardware does: cs8427.c writes DATAFLOW =
     * 0x0C, TXD = 01 (transmitter fed from the serial audio input port) with
     * TXOFF clear.
     *
     * DECLARED ONLY AFTER MEASURING IT (#184). The tempting test — play a tone
     * and look for it on the other unit — cannot answer this, because A also
     * reaches B through the analog cross-cable, so a dead transmitter yields
     * an identical positive. The measurement that does answer it put B on its
     * S/PDIF receiver AND slaved B's clock to A's carrier: the CS8427 has no
     * sample-rate converter, so without a carrier B has no master clock and
     * cannot produce a coherent capture at all. The tone returned with peak
     * exactly 0.5000, bit-identical to the source, which no analog round trip
     * can do; the silent control returned exact zeros rather than a noise
     * floor. See FINDING_187_spdif_output_is_real.md. */
    9, USB_DT_CS_INTERFACE, UAC_AC_OUTPUT_TERMINAL,
    TERM_SPDIF_OUT,
    UAC_TT_SPDIF & 0xFF, (UAC_TT_SPDIF >> 8) & 0xFF,
    0,                     /* bAssocTerminal */
    UNIT_FU_PLAYBACK,      /* #190 bSourceID = the same Feature Unit, so the
                            * mute covers BOTH physical outputs -- which is
                            * what the hardware does: 0x23.3 gates the C-port
                            * playback side that feeds the DAC and the AES3
                            * transmitter alike. */
    STR_SPDIF_OUT_IDX,     /* iTerminal (#204) */

    /* ---- Feature Unit: capture Mute (10 bytes) ---- #190
     *
     * After the Selector Unit and before the USB-in terminal, so it mutes
     * whichever source the selector has chosen -- analog or S/PDIF. Putting it
     * ahead of the selector would have needed one per input to mean the same
     * thing, and 0x23.2 is a single gate downstream of the mux in any case.
     *
     * Master channel only, and no Volume, for the same reasons as the playback
     * unit above. */
    FU_DESC_LEN, USB_DT_CS_INTERFACE, UAC_AC_FEATURE_UNIT,
    UNIT_FU_CAPTURE,
    UNIT_SELECTOR,         /* bSourceID = the analog/S-PDIF selector */
    1,                     /* bControlSize */
    UAC_FU_CTRL_MUTE,      /* bmaControls(0) master — Mute */
    0,                     /* bmaControls(1) ch1 */
    0,                     /* bmaControls(2) ch2 */
    0,                     /* iFeature */

    /* ---- Output Terminal: host USB-in stream (9 bytes) ---- */
    9, USB_DT_CS_INTERFACE, UAC_AC_OUTPUT_TERMINAL,
    TERM_USB_IN_STREAM,
    UAC_TT_USB_STREAMING & 0xFF, (UAC_TT_USB_STREAMING >> 8) & 0xFF,
    0,
    /* #160: the capture stream is now fed by the SELECTOR, not by the analog
     * terminal directly. This one byte is what puts the selector on the path
     * to the output terminal — and a host walks the topology backwards from
     * the output terminal, so a selector that nothing routes through is a
     * selector the host never finds, however correct its own descriptor is. */
    UNIT_FU_CAPTURE,       /* #190 bSourceID = the capture Feature Unit,
                            * which is itself fed by the selector. */
    0,


    /* ==================================================================
     * Interface 1: AudioStreaming — playback (host → device on EP2 OUT)
     * ================================================================== */

    /* Alt 0: zero-bandwidth (9 bytes) */
    9, USB_DT_INTERFACE,
    1, 0, 0,
    0x01, UAC_SUBCLASS_STREAM, 0x00, 0,

    /* Alt 1: active (9 bytes) */
    9, USB_DT_INTERFACE,
    1, 1, 2,               /* TWO endpoints: EP2 OUT data + EP2 IN feedback */
    0x01, UAC_SUBCLASS_STREAM, 0x00, 0,

    /* Class-specific AS General descriptor (7 bytes) */
    7, USB_DT_CS_INTERFACE, UAC_AS_GENERAL,
    TERM_USB_OUT_STREAM,   /* bTerminalLink */
    1,                     /* bDelay = 1 frame */
    0x01, 0x00,            /* wFormatTag = PCM */

    /* Type I Format descriptor with 2 discrete rates (14 bytes) */
    14, USB_DT_CS_INTERFACE, UAC_AS_FORMAT_TYPE,
    0x01,                  /* FORMAT_TYPE_I */
    AUDIO_NUM_CHANNELS,
    AUDIO_SUBFRAME_BYTES,
    AUDIO_BIT_RESOLUTION,
    0x02,                  /* bSamFreqType = 2 discrete rates */
    0x44, 0xAC, 0x00,      /* 44100 Hz */
    0x80, 0xBB, 0x00,      /* 48000 Hz */

    /* Standard AS isochronous endpoint (9 bytes — UAC1 form, +2 over vanilla)
     *
     * #185: ASYNCHRONOUS, not adaptive, and this is measured rather than
     * assumed. Adaptive claims the endpoint slaves its converter to the host.
     * Ours does not: the ACG free-runs from the crystal, proved twice over on
     * 2026-08-05 -- host-side by timestamped capture (#181/#182, the two units
     * differing by +4.263 +/- 0.989 ppm, which identical firmware on a shared
     * SOF cannot do) and device-side by ACGCAP (+4.53 ppm, agreeing to
     * 0.27 ppm). Under SOF-locking the two units would read identically.
     *
     * Asynchronous OUT obliges an explicit feedback endpoint, named here by
     * bSynchAddress. That is the honest arrangement and it is also TI's: the
     * "soft-PLL" the datasheet describes IS this endpoint, not a clock servo.
     * See FINDING_186_ti_softpll_is_the_feedback_endpoint.md. */
    9, USB_DT_ENDPOINT,
    EP_AUDIO_OUT,
    UAC_EP_ISO | UAC_EP_SYNC_ASYNC | UAC_EP_USAGE_DATA,
    AUDIO_MAX_PACKET_LEN & 0xFF, (AUDIO_MAX_PACKET_LEN >> 8) & 0xFF,
    1,                     /* bInterval = 1 ms */
    0,                     /* bRefresh — 0 on the DATA endpoint */
    EP_AUDIO_FEEDBACK,     /* bSynchAddress = the feedback endpoint below */

    /* Class-specific iso audio EP descriptor (7 bytes) */
    7, USB_DT_CS_ENDPOINT, UAC_AS_GENERAL,
    0x01,                  /* bmAttributes: sampling-freq control supported */
    0x00,                  /* bLockDelayUnits */
    0x00, 0x00,            /* wLockDelay */

    /* ---- Feedback endpoint, EP2 IN (9 bytes) ---- #186 stage 2
     *
     * Reports how fast this device is ACTUALLY consuming, so the host can size
     * its packets to match. It carries no audio and gets no class-specific
     * endpoint descriptor -- only the data endpoint does.
     *
     * Usage type FEEDBACK with sync type NONE (bmAttributes bits 5:4 = 01,
     * bits 3:2 = 00): a feedback endpoint is not itself synchronised to
     * anything, it is what the data endpoint is synchronised BY.
     *
     * 3 bytes, 10.14, samples per frame -- see AUDIO_FEEDBACK_LEN in usb.h for
     * the two independent confirmations of that format.
     *
     * bRefresh = 2, i.e. the host reads this every 2^2 = 4 frames. That is
     * TI's cadence: SoftPll.c averages four frames (fbCount 3,2,1,0) and arms
     * the endpoint once per group. */
    9, USB_DT_ENDPOINT,
    EP_AUDIO_FEEDBACK,
    UAC_EP_ISO | UAC_EP_USAGE_FEEDBACK,
    AUDIO_FEEDBACK_LEN & 0xFF, (AUDIO_FEEDBACK_LEN >> 8) & 0xFF,
    1,                     /* bInterval = 1 ms */
    2,                     /* bRefresh = 2 -> polled every 4 frames */
    0,                     /* bSynchAddress = none, this IS the feedback */


    /* ==================================================================
     * Interface 2: AudioStreaming — capture (device → host on EP1 IN)
     * ================================================================== */

    /* Alt 0: zero-bandwidth (9 bytes) */
    9, USB_DT_INTERFACE,
    2, 0, 0,
    0x01, UAC_SUBCLASS_STREAM, 0x00, 0,

    /* Alt 1: active (9 bytes) */
    9, USB_DT_INTERFACE,
    2, 1, 1,
    0x01, UAC_SUBCLASS_STREAM, 0x00, 0,

    /* Class-specific AS General descriptor (7 bytes) */
    7, USB_DT_CS_INTERFACE, UAC_AS_GENERAL,
    TERM_USB_IN_STREAM,
    1,
    0x01, 0x00,

    /* Type I Format descriptor (14 bytes) */
    14, USB_DT_CS_INTERFACE, UAC_AS_FORMAT_TYPE,
    0x01,
    AUDIO_NUM_CHANNELS,
    AUDIO_SUBFRAME_BYTES,
    AUDIO_BIT_RESOLUTION,
    0x02,
    0x44, 0xAC, 0x00,
    0x80, 0xBB, 0x00,

    /* Standard AS iso endpoint (9 bytes)
     *
     * #185: ASYNCHRONOUS, for the same measured reason as playback -- and
     * unlike playback it needs NO feedback endpoint. An async IN endpoint
     * simply delivers what it has and the host absorbs the variation. The
     * datasheet agrees this is the only option: there is one capture counter
     * and it always follows MCLKO's selection, so "MCLKO2 cannot be
     * synchronized to the incoming USB data stream", and synchronisation for
     * record "is handled by the handshaking protocol established between the
     * assigned DMA channel and the USB buffer manager" (§2.2.6). */
    9, USB_DT_ENDPOINT,
    EP_AUDIO_IN,
    UAC_EP_ISO | UAC_EP_SYNC_ASYNC | UAC_EP_USAGE_DATA,
    AUDIO_MAX_PACKET_LEN & 0xFF, (AUDIO_MAX_PACKET_LEN >> 8) & 0xFF,
    1,
    0,
    0,

    /* Class-specific iso audio EP descriptor (7 bytes) */
    7, USB_DT_CS_ENDPOINT, UAC_AS_GENERAL,
    0x01, 0x00, 0x00, 0x00,

};

/* --- String descriptors --------------------------------------------- */
const unsigned char __code AppStringLang[APP_STRING_LANG_LEN] = {
    4, USB_DT_STRING,
    0x09, 0x04              /* English (US) */
};

#ifdef MBOX_SERIAL_NCHAR
/* Per-unit serial, string #3. Values printed on the units; see BENCH_WIRING.md.
 * Absent unless the build selects a unit, so the default image is unchanged. */
const unsigned char __code AppStringSerial[APP_STRING_SERIAL_LEN] = {
    APP_STRING_SERIAL_LEN, USB_DT_STRING, MBOX_SERIAL_CHARS
};
#endif

const unsigned char __code AppStringMfr[APP_STRING_MFR_LEN] = {
    22, USB_DT_STRING,
    'D',0,'i',0,'g',0,'i',0,'d',0,'e',0,'s',0,'i',0,'g',0,'n',0
};

/* THE NAME EVERY OPERATING SYSTEM SHOWS THE USER -- Audio MIDI Setup, Logic's
 * device list, lsusb, Windows Device Manager. Worth getting right.
 *
 * It read "Mbox (classc" until 2026-08-16: an unclosed parenthesis, twelve
 * characters, with bLength = 26 correctly describing all twelve. So nothing was
 * ever truncated on the wire and no gate could have caught it -- the descriptor
 * was self-consistent and simply said the wrong thing. It surfaced only when
 * sox on macOS needed the literal device name to open the output, and the
 * literal name was visibly wrong. */
const unsigned char __code AppStringProduct[APP_STRING_PRODUCT_LEN] = {
    46, USB_DT_STRING,
    'M',0,'b',0,'o',0,'x',0,' ',0,'(',0,'c',0,'l',0,'a',0,'s',0,'s',0,
    '-',0,'c',0,'o',0,'m',0,'p',0,'l',0,'i',0,'a',0,'n',0,'t',0,')',0
};

/* #204 terminal-name strings. */

const unsigned char __code AppStrStrAnalogIn[STR_ANALOG_IN_LEN] = {
    STR_ANALOG_IN_LEN, 0x03,
    'A',0, 'n',0, 'a',0, 'l',0, 'o',0, 'g',0, ' ',0, 'I',0, 'n',0
};

const unsigned char __code AppStrStrSpdifIn[STR_SPDIF_IN_LEN] = {
    0x14, 0x03,
    'S',0, '/',0, 'P',0, 'D',0, 'I',0, 'F',0, ' ',0, 'I',0, 'n',0
};

const unsigned char __code AppStrStrLineOut[STR_LINE_OUT_LEN] = {
    0x12, 0x03,
    'L',0, 'i',0, 'n',0, 'e',0, ' ',0, 'O',0, 'u',0, 't',0
};

const unsigned char __code AppStrStrSpdifOut[STR_SPDIF_OUT_LEN] = {
    0x16, 0x03,
    'S',0, '/',0, 'P',0, 'D',0, 'I',0, 'F',0, ' ',0, 'O',0, 'u',0, 't',0
};
