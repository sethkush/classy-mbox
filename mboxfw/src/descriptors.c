/*
 * USB Audio Class 1.0 descriptor set for Mbox 1.
 *
 * Layout:
 *   Interface 0     — AudioControl
 *      IT USB-OUT (host→device)  → OT Line-Out           (playback path)
 *      capture path (#159), per-channel source select:
 *        IT ch1 {mic,line,inst} → SU ch1 ┐
 *                                        ├→ MU (fixed 2x2) → OT USB-IN
 *        IT ch2 {mic,line,inst} → SU ch2 ┘
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
    0x00,                   /* iSerialNumber  = none */
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

    /* ---- Capture front end: six mono Input Terminals (6 x 12 bytes) ----
     *
     * One per (channel, source) pair, mirroring the hardware: each channel's
     * front-panel button cycles its own 3-bit field in the mux word, so the
     * two channels are independent and are modelled independently here.
     *
     * bNrChannels = 1 with an explicit spatial position (FL for channel 1,
     * FR for channel 2) so the Mixer Unit below has an unambiguous mapping
     * from each mono path to its half of the stereo stream. */

    /* ch1 mic */
    12, USB_DT_CS_INTERFACE, UAC_AC_INPUT_TERMINAL,
    TERM_CH1_MIC,
    UAC_TT_MIC & 0xFF, (UAC_TT_MIC >> 8) & 0xFF,
    0,                      /* bAssocTerminal */
    1,                      /* bNrChannels */
    0x01, 0x00,             /* wChannelConfig = FL */
    0, 0,                   /* iChannel, iTerminal */

    /* ch1 line */
    12, USB_DT_CS_INTERFACE, UAC_AC_INPUT_TERMINAL,
    TERM_CH1_LINE,
    UAC_TT_LINE_IN & 0xFF, (UAC_TT_LINE_IN >> 8) & 0xFF,
    0, 1, 0x01, 0x00, 0, 0,

    /* ch1 inst */
    12, USB_DT_CS_INTERFACE, UAC_AC_INPUT_TERMINAL,
    TERM_CH1_INST,
    UAC_TT_ANALOG_CONN & 0xFF, (UAC_TT_ANALOG_CONN >> 8) & 0xFF,
    0, 1, 0x01, 0x00, 0, 0,

    /* ch2 mic */
    12, USB_DT_CS_INTERFACE, UAC_AC_INPUT_TERMINAL,
    TERM_CH2_MIC,
    UAC_TT_MIC & 0xFF, (UAC_TT_MIC >> 8) & 0xFF,
    0, 1, 0x02, 0x00,       /* wChannelConfig = FR */
    0, 0,

    /* ch2 line */
    12, USB_DT_CS_INTERFACE, UAC_AC_INPUT_TERMINAL,
    TERM_CH2_LINE,
    UAC_TT_LINE_IN & 0xFF, (UAC_TT_LINE_IN >> 8) & 0xFF,
    0, 1, 0x02, 0x00, 0, 0,

    /* ch2 inst */
    12, USB_DT_CS_INTERFACE, UAC_AC_INPUT_TERMINAL,
    TERM_CH2_INST,
    UAC_TT_ANALOG_CONN & 0xFF, (UAC_TT_ANALOG_CONN >> 8) & 0xFF,
    0, 1, 0x02, 0x00, 0, 0,

    /* ---- Selector Unit: channel 1 source (9 bytes) ----
     *
     * UAC1 §4.3.2.4. bLength = 6 + bNrInPins.
     *
     * PIN ORDER IS THE WIRE PROTOCOL. SET_CUR carries a 1-based index into
     * baSourceID, so pin 1 = mic, 2 = line, 3 = inst. usb.c maps those to the
     * stock source patterns 0x06/0x05/0x03 and mux.c publishes them. Keep this
     * order in step with sel_pin_to_pattern() -- they are one table split
     * across two files, and nothing checks that mechanically.
     *
     * Order also chosen to match the button walk (mic -> line -> inst), so a
     * host enum and the front panel enumerate the sources the same way. */
    9, USB_DT_CS_INTERFACE, UAC_AC_SELECTOR_UNIT,
    UNIT_SEL_CH1,
    3,                      /* bNrInPins */
    TERM_CH1_MIC, TERM_CH1_LINE, TERM_CH1_INST,
    0,                      /* iSelector */

    /* ---- Selector Unit: channel 2 source (9 bytes) ---- */
    9, USB_DT_CS_INTERFACE, UAC_AC_SELECTOR_UNIT,
    UNIT_SEL_CH2,
    3,
    TERM_CH2_MIC, TERM_CH2_LINE, TERM_CH2_INST,
    0,

    /* ---- Mixer Unit: fixed 2x2 (12 bytes) ----
     *
     * UAC1 §4.3.2.3. bLength = 9 + bNrInPins + bmControls size.
     *
     * Present ONLY because an Output Terminal has exactly one bSourceID, so
     * two mono selector paths cannot otherwise reach one stereo terminal.
     * bmControls = 0: no crosspoint is programmable, so this is fixed unity
     * routing (ch1 -> FL, ch2 -> FR) and a host creates no controls for it.
     *
     * Deliberately NOT used to expose the mono fold-down. That switch is a
     * single hardware bit in the codec word, not a gain matrix, so
     * advertising programmable crosspoints would promise the host arithmetic
     * this device cannot perform. Seth: the mono switch affects the headphone
     * output only, so it is out of the capture path entirely. */
    13, USB_DT_CS_INTERFACE, UAC_AC_MIXER_UNIT,
    UNIT_MIXER,
    2,                      /* bNrInPins */
    UNIT_SEL_CH1, UNIT_SEL_CH2,
    2,                      /* bNrChannels out */
    0x03, 0x00,             /* wChannelConfig = FL + FR */
    0,                      /* iChannelNames */
    0x00,                   /* bmControls — no programmable crosspoints */
    0,                      /* iMixer */

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
    UNIT_MIXER,            /* bSourceID = the 2x2 mixer, i.e. both selectors */
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

const unsigned char __code AppStringMfr[APP_STRING_MFR_LEN] = {
    22, USB_DT_STRING,
    'D',0,'i',0,'g',0,'i',0,'d',0,'e',0,'s',0,'i',0,'g',0,'n',0
};

const unsigned char __code AppStringProduct[APP_STRING_PRODUCT_LEN] = {
    26, USB_DT_STRING,
    'M',0,'b',0,'o',0,'x',0,' ',0,'(',0,'c',0,'l',0,'a',0,'s',0,'s',0,'c',0
};
