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

/* Config descriptor attribute bits */
#define USB_CFG_BUS_POWERED    0x80
#define USB_CFG_SELF_POWERED   0xC0
#define USB_CFG_REMOTE_WAKE    0xA0

/* Device / vendor IDs — reuse Digi's so macOS still identifies as Mbox */
#define MBOX_VID               0x0DBA
#define MBOX_PID               0x1000
#define MBOX_BCD_DEVICE        0x0100  /* our custom-fw v1.0 */

/* Endpoint addresses */
#define EP_AUDIO_IN            0x81    /* EP1 IN  = capture  (device → host) */
#define EP_AUDIO_OUT           0x02    /* EP2 OUT = playback (host → device) */

/* Terminal IDs — arbitrary within one AC interface, must be unique */
#define TERM_USB_OUT_STREAM    0x01   /* host → device audio (playback data)  */
#define TERM_ANALOG_IN         0x02   /* mic/line/inst analog capture         */
#define TERM_LINE_OUT          0x03   /* analog line output                   */
#define TERM_USB_IN_STREAM     0x04   /* device → host audio (capture data)   */

/* Audio format */
#define AUDIO_NUM_CHANNELS     2
#define AUDIO_SUBFRAME_BYTES   3    /* 24-bit */
#define AUDIO_BIT_RESOLUTION   24

/* Max packet sizes at 48 kHz + a slack byte for jitter: 2ch × 3B × 48 = 288.
 * Bump slightly for the adaptive-endpoint slew allowance. */
#define AUDIO_MAX_PACKET_LEN   294

#endif /* MBOXFW_USB_H */
