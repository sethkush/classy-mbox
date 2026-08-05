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
    0,                      /* bNumEndpoints — control uses EP0 */
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
    UAC_TT_LINE_IN & 0xFF, (UAC_TT_LINE_IN >> 8) & 0xFF,
    0,                      /* bAssocTerminal */
    2,                      /* bNrChannels */
    0x03, 0x00,             /* wChannelConfig = FL + FR */
    0, 0,                   /* iChannel, iTerminal */

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
    0, 0,                   /* iChannel, iTerminal */

    /* ---- Selector Unit: analog vs S/PDIF (8 bytes) ---- #160
     *
     * bLength = 6 + bNrInPins per UAC1 §4.3.2.4. Pin ORDER IS THE PROTOCOL:
     * SET_CUR/GET_CUR carry a 1-based index into baSourceID, so pin 1 =
     * analog and pin 2 = S/PDIF. That is exactly the encoding the handler in
     * usb.c implements and the kernel quirk documents ("ANALOG Source -> 0x01,
     * S/PDIF Source -> 0x02"). Swapping these two bytes would silently invert
     * the control on every host.
     *
     * Why one selector and not two: mboxfw briefly declared a per-channel
     * Selector Unit each (#159) and that was removed, because Apple's driver
     * creates every input selector as kIOAudioControlChannelIDAll and cannot
     * express per-channel source select at all — see
     * FINDING_macos_one_input_selector.md. A SINGLE selector choosing between
     * two whole signal paths is precisely the model that driver does have
     * ("items are PATHS, not pins"), so this is the shape that suits both
     * hosts rather than the one that suited only Linux. */
    8, USB_DT_CS_INTERFACE, UAC_AC_SELECTOR_UNIT,
    UNIT_SELECTOR,
    2,                      /* bNrInPins */
    TERM_ANALOG_IN,         /* baSourceID(1) — position 1 = analog */
    TERM_SPDIF_IN,          /* baSourceID(2) — position 2 = S/PDIF */
    0,                      /* iSelector */

    /* ---- Output Terminal: analog line-out (9 bytes) ---- */
    9, USB_DT_CS_INTERFACE, UAC_AC_OUTPUT_TERMINAL,
    TERM_LINE_OUT,
    UAC_TT_LINE_OUT & 0xFF, (UAC_TT_LINE_OUT >> 8) & 0xFF,
    0,
    TERM_USB_OUT_STREAM,   /* bSourceID = playback stream */
    0,

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
    UNIT_SELECTOR,         /* bSourceID = the analog/S-PDIF selector */
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
    1, 1, 1,               /* one endpoint (EP2 OUT) */
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

    /* Standard AS isochronous endpoint (9 bytes — UAC1 form, +2 over vanilla) */
    9, USB_DT_ENDPOINT,
    EP_AUDIO_OUT,
    UAC_EP_ISO | UAC_EP_SYNC_ADAPTIVE,
    AUDIO_MAX_PACKET_LEN & 0xFF, (AUDIO_MAX_PACKET_LEN >> 8) & 0xFF,
    1,                     /* bInterval = 1 ms */
    0,                     /* bRefresh */
    0,                     /* bSynchAddress = none (adaptive) */

    /* Class-specific iso audio EP descriptor (7 bytes) */
    7, USB_DT_CS_ENDPOINT, UAC_AS_GENERAL,
    0x01,                  /* bmAttributes: sampling-freq control supported */
    0x00,                  /* bLockDelayUnits */
    0x00, 0x00,            /* wLockDelay */


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

    /* Standard AS iso endpoint (9 bytes) */
    9, USB_DT_ENDPOINT,
    EP_AUDIO_IN,
    UAC_EP_ISO | UAC_EP_SYNC_ADAPTIVE,
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

const unsigned char __code AppStringProduct[APP_STRING_PRODUCT_LEN] = {
    26, USB_DT_STRING,
    'M',0,'b',0,'o',0,'x',0,' ',0,'(',0,'c',0,'l',0,'a',0,'s',0,'s',0,'c',0
};
