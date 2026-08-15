#ifndef MBOXFW_USB_H
#define MBOXFW_USB_H
/*
 * USB / UAC1 constants used by descriptors.c and usb.c.
 * Names follow the USB 2.0 spec and USB Audio Class 1.0 spec.
 */

/* Standard descriptor types */
#define USB_DT_DEVICE          0x01
#define USB_DT_CONFIG          0x02
#define USB_DT_STRING          0x03
#define USB_DT_INTERFACE       0x04
#define USB_DT_ENDPOINT        0x05

/* Class-specific descriptor types */
#define USB_DT_CS_INTERFACE    0x24
#define USB_DT_CS_ENDPOINT     0x25

/* Audio Control interface subclass codes */
#define UAC_SUBCLASS_UNDEF     0x00
#define UAC_SUBCLASS_CONTROL   0x01
#define UAC_SUBCLASS_STREAM    0x02

/* Class-specific AC interface descriptor subtypes */
#define UAC_AC_HEADER          0x01
#define UAC_AC_INPUT_TERMINAL  0x02
#define UAC_AC_OUTPUT_TERMINAL 0x03
#define UAC_AC_MIXER_UNIT      0x04
#define UAC_AC_SELECTOR_UNIT   0x05
#define UAC_AC_FEATURE_UNIT    0x06

/* Class-specific AS interface descriptor subtypes */
#define UAC_AS_GENERAL         0x01
#define UAC_AS_FORMAT_TYPE     0x02

/* Terminal types (USB Audio Terminal Types spec, section 2) */
#define UAC_TT_USB_STREAMING   0x0101   /* USB endpoint as terminal */
#define UAC_TT_MIC             0x0201   /* generic microphone */
#define UAC_TT_LINE_IN         0x0603   /* line connector */
#define UAC_TT_SPDIF           0x0605   /* S/PDIF interface */
#define UAC_TT_LINE_OUT        0x0603
#define UAC_TT_SPEAKER         0x0301
#define UAC_TT_HEADPHONES      0x0302

/* Endpoint attribute bits (bmAttributes) */
#define UAC_EP_ISO             0x01
#define UAC_EP_SYNC_ASYNC      0x04
#define UAC_EP_SYNC_ADAPTIVE   0x08
#define UAC_EP_SYNC_SYNC       0x0C
/* bmAttributes bits 5:4, usage type (USB 2.0 Table 9-13). A feedback endpoint
 * is usage type 01 with sync type 00 -- it is not itself synchronised to
 * anything, it is what the other endpoint is synchronised BY. */
#define UAC_EP_USAGE_DATA      0x00
#define UAC_EP_USAGE_FEEDBACK  0x10

/* Config descriptor attribute bits */
#define USB_CFG_BUS_POWERED    0x80
#define USB_CFG_SELF_POWERED   0xC0
#define USB_CFG_REMOTE_WAKE    0xA0

/* Device / vendor IDs — reuse Digi's so macOS still identifies as Mbox */
#define MBOX_VID               0x0DBA
/* Overridable at build time: `make MBOX_PID=0x2000`.
 *
 * Linux's snd-usb-audio matches a composite quirk on VID:PID 0dba:1000
 * (sound/usb/quirks-table.h) and applies hardcoded fixed streams WITHOUT
 * parsing our descriptors — so a fully class-compliant device still gets
 * hijacked and probe fails -22. That quirk exists for Digidesign's original
 * non-class-compliant firmware; we inherit a workaround for a problem we
 * deliberately do not have.
 *
 * Building with a PID outside the quirk table makes Linux treat us as the
 * generic UAC1 device we are, which is the honest test of class compliance.
 * Do NOT reshape the descriptors to satisfy the quirk instead — that would
 * make the firmware less standard, not more. */
#ifndef MBOX_PID
#define MBOX_PID               0x1000
#endif
#define MBOX_BCD_DEVICE        0x0100  /* our custom-fw v1.0 */

/* Endpoint addresses */
#define EP_AUDIO_IN            0x81    /* EP1 IN  = capture  (device → host) */
#define EP_AUDIO_OUT           0x02    /* EP2 OUT = playback (host → device) */
/* #186 stage 2. EP2 IN = the playback feedback endpoint. Same endpoint number
 * as the OUT it serves, which is the usual pairing and is TI's choice too --
 * SoftPll.c writes INEP2_X and arms IEPDCNTX2. Its register block (IEPCNF2 =
 * 0xFF58) was free, and 0x81 was already taken by capture. */
#define EP_AUDIO_FEEDBACK      0x82
/* 3 bytes, 10.14, samples per frame. Full-speed feedback is 3 bytes; the
 * format is confirmed twice over -- TI's SoftPll.c builds (nInt << 14) |
 * (nFrac << 4), and the Linux driver states "full speed devices report
 * feedback values in 10.14 format as samples per frame". Linux RANGE-CHECKS
 * the value and silently falls back to nominal when it is out of band, so a
 * mis-scaled value presents as "the endpoint does nothing" rather than as an
 * error. */
#define AUDIO_FEEDBACK_LEN     3

/* Terminal IDs — arbitrary within one AC interface, must be unique */
#define TERM_USB_OUT_STREAM    0x01   /* host → device audio (playback data)  */
#define TERM_ANALOG_IN         0x02   /* mic/line/inst analog capture         */
#define TERM_LINE_OUT          0x03   /* analog line output                   */
#define TERM_USB_IN_STREAM     0x04   /* device → host audio (capture data)   */
/* #160. These two are NOT arbitrary despite the comment above — they are
 * stock's own numbering, and matching it makes three things agree that
 * previously only agreed in pairs:
 *
 *   - rev20_descriptors_decoded.md records SU bUnitID = 5 with
 *     baSourceID = [2 (Analog), 6 (S/PDIF)] in the stock UAC block;
 *   - the kernel quirk hardcodes wIndex = 0x0500 = (unit 5 << 8) | AC iface
 *     (reference/mbox1_mixer_quirks.c.snippet);
 *   - mboxfw's handler has answered unit 5 since #177.
 *
 * Until now mboxfw served no Selector Unit at all, so the handler answered a
 * control that nothing advertised — stock's arrangement, and the reason Linux
 * needs a quirk rather than generic UAC support. Declaring it makes the
 * advertised topology true and the control discoverable with no quirk. */
#define TERM_SPDIF_IN          0x06   /* S/PDIF receiver as a capture source  */
#define UNIT_SELECTOR          0x05   /* analog-vs-S/PDIF Selector Unit       */
/* #187. The AES3 TRANSMITTER, which has been running since the first build and
 * which no host could see. cs8427.c sets DATAFLOW = 0x0C — TXD = 01
 * (CS8427_TXDSERIAL, transmitter fed from the serial audio input port) with
 * TXOFF clear — so the RCA digital output carries the playback side of the
 * C-port, in parallel with the analog line out.
 *
 * MEASURED before being declared (#184, 2026-08-05), because a declared
 * terminal that turns out to be silent is worse than no terminal at all: unit
 * A played a 1 kHz tone, unit B captured it through its S/PDIF receiver while
 * slaved to A's carrier, and the tone came back with peak EXACTLY 0.5000 —
 * bit-identical to the source amplitude, which an analog round trip cannot
 * produce. The silent control returned exact zeros rather than a noise floor.
 * See FINDING_187_spdif_output_is_real.md. */
#define TERM_SPDIF_OUT         0x07   /* AES3 transmitter as a playback sink  */
/* #203. The instrument (Hi-Z) front end as its own capture source.
 *
 * Same physical 1/4" jack as TERM_ANALOG_IN -- the 74HC157 muxes pick which
 * front end feeds the shared gain stage, and they are genuinely different
 * circuits: measured 18.9 dB apart in sensitivity at an identical dial
 * position, interleaved, with both LINE arms agreeing to 0.01 dB.
 * FINDING_196_gain_curve_and_the_bad_TS_cable.md. */
#define TERM_INST_IN           0x0A   /* instrument/Hi-Z analog capture source */
                                      /* 0x08/0x09 are the two Feature Units */
/* #190. One Feature Unit per path, each carrying a single Mute control on the
 * master channel. Two units and not one because #189 measured the hardware to
 * BE two: 0x23.3 gates playback and 0x23.2 gates capture, each killing exactly
 * one direction. A single Feature Unit over one global gate would have been a
 * control that lies about the other stream, which is why #46 refused to
 * declare one until the measurement existed. */
#define UNIT_FU_PLAYBACK       0x08   /* Mute on the playback path (0x23.3)   */
#define UNIT_FU_CAPTURE        0x09   /* Mute on the capture path  (0x23.2)   */

/* UAC1 §4.3.2.5 bmaControls bit 0. Master channel only -- see descriptors.c. */
#define UAC_FU_CTRL_MUTE       0x01
#define UAC_FU_MUTE_CONTROL    0x01   /* control selector on wValueH          */

/* bLength = 7 + (bNrChannels + 1) * bControlSize, i.e. 7 + 3*1 for a stereo
 * unit with a master entry and one byte of controls per channel. */
#define FU_DESC_LEN            10

/* Audio format */
#define AUDIO_NUM_CHANNELS     2
#define AUDIO_SUBFRAME_BYTES   3    /* 24-bit */
#define AUDIO_BIT_RESOLUTION   24

/* Max packet sizes at 48 kHz + a slack byte for jitter: 2ch × 3B × 48 = 288.
 * Bump slightly for the adaptive-endpoint slew allowance. */
#define AUDIO_MAX_PACKET_LEN   294



/* --- Descriptor lengths ---------------------------------------------
 *
 * Shared by descriptors.c (which sizes the arrays) and usb.c (which
 * passes them to stage_reply). Single definition so the two can never
 * drift apart.
 *
 * These MUST be compile-time constants at the stage_reply call sites.
 * Passing a runtime `Arr[0]` read instead caused safety_net to deliver
 * only 16 of 18 bytes of a string descriptor and then hang the transfer
 * — see the block comment in usb.c handle_get_descriptor(). */
#define APP_DEV_DESC_LEN        18
#define APP_STRING_LANG_LEN     4
#define APP_STRING_MFR_LEN      22   /* 2 + 2*10 "Digidesign"   */
#define APP_STRING_PRODUCT_LEN  26   /* 2 + 2*12 "Mbox (classc" */

/* iSerialNumber — string #3, optional, per-unit.
 *
 * WHY THIS EXISTS. Two Mboxes share the bench, and until now they were told
 * apart by giving each a different MBOX_PID. That works but is the wrong
 * mechanism twice over: the PID says which PRODUCT this is, not which UNIT,
 * so a per-unit PID makes two identical devices claim to be different models
 * — and it changes driver binding, which is exactly the variable an A/B
 * measurement must hold still. It also nearly caused a collision: the #171
 * experiment image was built for A's PID and would have given B the same PID
 * as A had it been flashed unchanged.
 *
 * A serial number is what USB provides for "which unit". With it, both units
 * build at MBOX_PID=0x2000 — same product identity, same quirk handling, same
 * binding — and the host still addresses either one unambiguously
 * (`usb.core.find(serial_number=...)`, or the `serial` attribute under
 * /sys/bus/usb/devices/).
 *
 * `make MBOX_UNIT=A` / `MBOX_UNIT=B` selects one. Undefined builds serve no
 * serial and set iSerialNumber = 0, which is the previous behaviour exactly,
 * so the default image is unchanged.
 *
 * The values are the ones printed on the units, recorded in BENCH_WIRING.md.
 * Spelled as UTF-16LE characters rather than a C string because a USB string
 * descriptor is 16-bit, and doing the conversion at run time would add code to
 * a descriptor path that is currently a straight table copy. */
#if defined(MBOX_SERIAL_A)
#  define MBOX_SERIAL_CHARS  'R',0,'K',0,'1',0,'0',0,'8',0,'7',0,'4',0,'6',0, \
                             '0',0,'0',0,'Q',0            /* RK10874600Q */
#  define MBOX_SERIAL_NCHAR  11
#elif defined(MBOX_SERIAL_B)
#  define MBOX_SERIAL_CHARS  'R',0,'K',0,'1',0,'6',0,'7',0,'2',0,'5',0,'0',0, \
                             '0',0,'M',0                  /* RK1672500M */
#  define MBOX_SERIAL_NCHAR  10
#endif

#ifdef MBOX_SERIAL_NCHAR
#  define APP_STRING_SERIAL_LEN  (2 + 2 * MBOX_SERIAL_NCHAR)
#  define APP_ISERIAL            0x03
#else
#  define APP_ISERIAL            0x00
#endif

/* Total configuration-bundle length. Sum of every descriptor in
 * descriptors.c, in host-parse order. Edit both together. */
/*            header  IT-usb  IT-analog  IT-spdif  SU  OT-lineout  OT-usbin
 *            OT-spdifout
 * #160 added the S/PDIF input terminal (12) and the Selector Unit (6 + 2
 * source pins = 8). #187 added the S/PDIF OUTPUT terminal (9). #203 added the
 * instrument input terminal (12) and grew the Selector by one source pin
 * (8 -> 9). A host that reads a short wTotalLength silently ignores every unit
 * past it, which presents as "the selector does not exist" rather than as a
 * descriptor error — so this and the array below are the same fact twice and
 * have to move together. verify_descriptors.py caught exactly that here: the
 * declared 101 against 114 bytes actually emitted. */
#define AC_BLOCK_LEN        (10 + 12 + 12 + 12 + 12 + 9 + 9 + 9 + 9 \
                             + FU_DESC_LEN + FU_DESC_LEN)

/* Per streaming interface: alt 0 (9) + alt 1 (9 + 7 + 14 + 9 + 7).
 *
 * THE TWO ARE NO LONGER THE SAME LENGTH. #186 stage 2 gives the PLAYBACK
 * interface a second endpoint -- the 9-byte feedback endpoint descriptor --
 * so it runs 9 bytes longer than capture. They shared one constant until
 * 2026-08-05, and a single constant would now understate wTotalLength by 9:
 * the host would stop parsing 9 bytes early, which lands inside the capture
 * interface and presents as "the capture endpoint does not exist" rather than
 * as a descriptor error. Keep the two separate for exactly that reason. */
#define AS_IFACE_CAPTURE_LEN   (9 + (9 + 7 + 14 + 9 + 7))
#define AS_IFACE_PLAYBACK_LEN  (AS_IFACE_CAPTURE_LEN + 9)

#define APP_CFG_TOTAL_LEN   (9 + 9 + AC_BLOCK_LEN \
                             + AS_IFACE_PLAYBACK_LEN + AS_IFACE_CAPTURE_LEN)

/* Set by the Digi enter-DFU class request handler, consumed by main().
 * See handle_digi_enter_dfu() in usb.c for why the work is deferred. */
extern volatile __data unsigned char g_dfu_request_pending;

/* Non-zero once SET_CONFIGURATION has selected a non-zero configuration.
 * The suspend path in power.c gates on this, mirroring stock's test of
 * RAM[0x21].6 at Rev 20 0x0526 / Rev 22 0x0525. */
unsigned char usb_is_configured(void);

/* EP0 buffer + count setup, re-runnable on resume. Stock calls the same
 * routine from its resume tail (Rev 20 0x0554 -> fcn.0x0970). */
void usb_ep0_setup(void);

#endif /* MBOXFW_USB_H */
