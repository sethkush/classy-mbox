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
/* Instrument (high-Z DI) has no dedicated UAC1 terminal type. 0x0601
 * "Analog connector" is the least-wrong of the 0x06xx external types, and
 * picking a DISTINCT type per source matters beyond tidiness: snd-usb-audio
 * names Selector Unit enum entries from the source terminal types, so three
 * different types give three readable names in alsamixer instead of three
 * copies of one. */
#define UAC_TT_ANALOG_CONN     0x0601   /* analog connector — used for inst */
#define UAC_TT_SPDIF           0x0605   /* S/PDIF interface */
#define UAC_TT_LINE_OUT        0x0603
#define UAC_TT_SPEAKER         0x0301
#define UAC_TT_HEADPHONES      0x0302

/* Endpoint attribute bits (bmAttributes) */
#define UAC_EP_ISO             0x01
#define UAC_EP_SYNC_ASYNC      0x04
#define UAC_EP_SYNC_ADAPTIVE   0x08
#define UAC_EP_SYNC_SYNC       0x0C

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

/* Terminal / Unit IDs — arbitrary within one AC interface, must be unique
 * and non-zero.
 *
 * CAPTURE TOPOLOGY (#159). The single TERM_ANALOG_IN that used to sit here
 * was replaced on 2026-08-03 by the real thing: each of the two channels has
 * its own mic/line/inst switch on the front panel, so each gets three mono
 * Input Terminals feeding its own Selector Unit, and a fixed 2x2 Mixer Unit
 * recombines the two mono selectors into the stereo capture stream.
 *
 *   IT ch1 mic  ─┐
 *   IT ch1 line ─┼─> SU ch1 ─┐
 *   IT ch1 inst ─┘           ├─> MU (fixed) ─> OT USB-IN
 *   IT ch2 mic  ─┐           │
 *   IT ch2 line ─┼─> SU ch2 ─┘
 *   IT ch2 inst ─┘
 *
 * The obvious cheaper shape -- three STEREO input terminals into one stereo
 * Selector Unit -- was rejected: it is ~60 bytes smaller but yields a single
 * control that forces both channels to the same source, which the hardware
 * does not do and the front panel does not do. A class control that cannot
 * express what the buttons express is worse than no class control.
 *
 * The Mixer Unit exists only because UAC1 has no other way to merge two mono
 * paths into one stereo terminal (an Output Terminal has exactly one
 * bSourceID). Its bmControls is all zero -- no programmable crosspoints, so
 * it is fixed unity routing and hosts create no controls for it. */
#define TERM_USB_OUT_STREAM    0x01   /* host → device audio (playback data)  */
#define TERM_LINE_OUT          0x03   /* analog line output                   */
#define TERM_USB_IN_STREAM     0x04   /* device → host audio (capture data)   */

#define TERM_CH1_MIC           0x05
#define TERM_CH1_LINE          0x06
#define TERM_CH1_INST          0x07
#define TERM_CH2_MIC           0x08
#define TERM_CH2_LINE          0x09
#define TERM_CH2_INST          0x0A
#define UNIT_SEL_CH1           0x0B   /* Selector Unit, channel 1 source      */
#define UNIT_SEL_CH2           0x0C   /* Selector Unit, channel 2 source      */
#define UNIT_MIXER             0x0D   /* fixed 2x2, no programmable controls  */

/* UAC1 §5.2.2.4.1: the only control a Selector Unit has. */
#define UAC_SELECTOR_CTRL      0x01

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

/* Total configuration-bundle length. Sum of every descriptor in
 * descriptors.c, in host-parse order. Edit both together. */
/* AC block: header 10 + IT(USB-out) 12 + six mono ITs 6*12 + two SUs 2*9
 * + MU 12 + OT(line-out) 9 + OT(USB-in) 9.
 * SU  bLength = 6 + bNrInPins            = 6 + 3 = 9
 * MU  bLength = 10 + bNrInPins + bmSize  = 10 + 2 + 1 = 13 (2x2 = 4 bits)
 *
 * The MU line read `9 + p + N` on the first pass, which is a spec misreading
 * rather than a typo: the Mixer Unit carries wChannelConfig (2 bytes) where
 * the Selector Unit carries nothing, so its fixed overhead is 10, not 9. The
 * emitted bytes were right at 13 and only this arithmetic was short, so it
 * surfaced as "excess elements in array initializer" -- SDCC caught what a
 * host would have seen as a truncated AC block. */
#define AC_BLOCK_LEN        (10 + 12 + (6 * 12) + (2 * 9) + 13 + 9 + 9)

#define APP_CFG_TOTAL_LEN   (9 + 9 + AC_BLOCK_LEN \
                              + 9 + 9 + 7 + 14 + 9 + 7 \
                              + 9 + 9 + 7 + 14 + 9 + 7)

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
