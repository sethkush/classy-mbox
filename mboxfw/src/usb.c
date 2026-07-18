/*
 * USB engine — EP0 setup handling.
 *
 * Handles standard USB requests (GET_DESCRIPTOR, SET_CONFIGURATION,
 * SET/GET_INTERFACE) and the UAC1 class requests our descriptor set
 * declares. Streaming (EP1 IN, EP2 OUT) is set up here and driven from
 * usb.h's audio buffer scheduler.
 *
 * TAS1020A EP0 protocol summary (from Rev 20 disassembly + TI ROM code):
 *   - SETUP packet arrives → firmware inspects SETPACK block at 0xFF28.
 *   - If host is asking for data (bmReq bit 7 = 1), we write reply
 *     into the EP0 IN buffer (base 0xFA10 in Rev 20) and set IEPBCTX0
 *     to the byte count, then hardware sends it.
 *   - If host is sending data (bmReq bit 7 = 0), OEPBCTX0 holds the
 *     received count and we read from EP0 OUT buffer (base 0xFA18).
 *   - Status stage is armed by writing zero to the opposite endpoint's
 *     byte-count register.
 */

#include "regs.h"
#include "usb.h"

/* Descriptor tables from descriptors.c */
extern const __code unsigned char AppDevDesc[];
extern const __code unsigned char AppConfigDesc[];
extern const __code unsigned char AppStringLang[];
extern const __code unsigned char AppStringMfr[];
extern const __code unsigned char AppStringProduct[];

/* Current USB device state — updated by SET_CONFIGURATION / SET_INTERFACE. */
static __data unsigned char g_configured = 0;
static __data unsigned char g_alt_playback = 0;   /* alt setting on interface 1 */
static __data unsigned char g_alt_capture  = 0;   /* alt setting on interface 2 */
static __data unsigned long g_sample_rate  = 48000UL;   /* 24-bit BE on the wire */

/* EP0 IN reply staging.
 * Rev 20's EP0 IN buffer sits at 0xFA10; we mirror that here. On a real
 * transfer larger than the 8-byte EP0 MaxPacketSize we'll need to chunk
 * across multiple SETUP→IN cycles — tracked with these pointers. */
static __code const unsigned char *g_ep0_reply_src = 0;
static __data unsigned int          g_ep0_reply_remaining = 0;

/* --- SETPACK access helpers (matches Rev 20 setup dispatcher @ 0x0026) --- */
#define bmReq   SETPACK_BMREQ
#define bReq    SETPACK_BREQ
#define wValueL SETPACK_WVAL_L
#define wValueH SETPACK_WVAL_H
#define wIndexL SETPACK_WIDX_L
#define wIndexH SETPACK_WIDX_H
#define wLenL   SETPACK_WLEN_L
#define wLenH   SETPACK_WLEN_H

/* Standard USB request codes (per USB 2.0 spec table 9-4) */
#define REQ_GET_STATUS       0x00
#define REQ_CLEAR_FEATURE    0x01
#define REQ_SET_FEATURE      0x03
#define REQ_SET_ADDRESS      0x05
#define REQ_GET_DESCRIPTOR   0x06
#define REQ_GET_CONFIG       0x08
#define REQ_SET_CONFIG       0x09
#define REQ_GET_INTERFACE    0x0A
#define REQ_SET_INTERFACE    0x0B

/* UAC1 class request codes */
#define UAC_SET_CUR          0x01
#define UAC_GET_CUR          0x81
#define UAC_GET_MIN          0x82
#define UAC_GET_MAX          0x83
#define UAC_GET_RES          0x84
/* UAC1 endpoint control selectors */
#define UAC_EP_SAMPLING_FREQ_CTRL   0x01


/* --- EP0 IN reply setup helpers --- */

static void reply_stall(void)
{
    /* Rev 20 stalls EP0 by clearing bits in IEPCNF0 & 0xD7. We mirror. */
    IEPCNF0 &= 0xD7;
    OEPCNF0 &= 0xD7;
}

static void reply_zero_length(void)
{
    /* Zero-length IN packet acknowledges an OUT-only request (SET_CONFIG etc.) */
    IEPBCTX0 = 0;
}

static void stage_reply(__code const unsigned char *src, unsigned int len)
{
    /* Cap reply length to what the host actually asked for. */
    unsigned int wLen = ((unsigned int)wLenH << 8) | wLenL;
    if (len > wLen) len = wLen;
    g_ep0_reply_src = src;
    g_ep0_reply_remaining = len;
    /* First chunk fires immediately; usb_service() drives the rest. */
    /* TODO: byte-by-byte copy into EP0 IN buffer at 0xFA10 goes here. */
}


/* --- Standard request dispatchers --- */

static void handle_get_descriptor(void)
{
    unsigned char type  = wValueH;
    unsigned char index = wValueL;

    switch (type) {
        case USB_DT_DEVICE:
            stage_reply(AppDevDesc, AppDevDesc[0]);
            break;
        case USB_DT_CONFIG:
            /* wTotalLength is at bytes [2..3] of the config descriptor. */
            stage_reply(AppConfigDesc,
                        AppConfigDesc[2] | ((unsigned int)AppConfigDesc[3] << 8));
            break;
        case USB_DT_STRING:
            switch (index) {
                case 0:  stage_reply(AppStringLang,    AppStringLang[0]);    break;
                case 1:  stage_reply(AppStringMfr,     AppStringMfr[0]);     break;
                case 2:  stage_reply(AppStringProduct, AppStringProduct[0]); break;
                default: reply_stall(); break;
            }
            break;
        default:
            reply_stall();
            break;
    }
}

static void handle_set_configuration(void)
{
    g_configured = wValueL;
    reply_zero_length();
}

static void handle_set_interface(void)
{
    unsigned char iface = wIndexL;
    unsigned char alt   = wValueL;

    if (iface == 1) {
        g_alt_playback = alt;
        /* TODO: (de)activate OEPCNF2 based on alt. */
    } else if (iface == 2) {
        g_alt_capture = alt;
        /* TODO: (de)activate IEPCNF1 based on alt. */
    }
    reply_zero_length();
}

static void handle_get_interface(void)
{
    unsigned char iface = wIndexL;
    static __data unsigned char alt_reply;

    if (iface == 1)      alt_reply = g_alt_playback;
    else if (iface == 2) alt_reply = g_alt_capture;
    else                 alt_reply = 0;
    /* Single-byte reply. TODO: stage into EP0 IN buffer. */
    (void)alt_reply;
}


/* --- UAC1 class request dispatcher --- */

static void handle_class_endpoint_request(void)
{
    unsigned char selector = wValueH;   /* CS on wValueH per UAC1 spec */
    if (selector != UAC_EP_SAMPLING_FREQ_CTRL) {
        reply_stall();
        return;
    }
    if (bReq == UAC_SET_CUR) {
        /* Host sending 3-byte sample rate. Read from EP0 OUT buffer. */
        /* TODO: pull 3 bytes from 0xFA18, compose into g_sample_rate,
         *       then reconfigure the C-port + CS8427 clock. */
        reply_zero_length();
    } else if (bReq == UAC_GET_CUR) {
        /* Reply with current sample rate as 3-byte LE. */
        static __data unsigned char rate_bytes[3];
        rate_bytes[0] = g_sample_rate & 0xFF;
        rate_bytes[1] = (g_sample_rate >> 8) & 0xFF;
        rate_bytes[2] = (g_sample_rate >> 16) & 0xFF;
        /* TODO: stage into EP0 IN buffer. */
        (void)rate_bytes;
    } else {
        reply_stall();
    }
}


/* --- Top-level SETUP dispatch (mirrors Rev 20 fcn.0x00 @ 0x0026) --- */

static void handle_setup(void)
{
    unsigned char reqtype = bmReq & 0x60;   /* mask off type field */
    unsigned char recip   = bmReq & 0x1F;

    if (reqtype == 0x00) {
        /* Standard request */
        switch (bReq) {
            case REQ_GET_DESCRIPTOR:   handle_get_descriptor();   break;
            case REQ_SET_CONFIG:       handle_set_configuration(); break;
            case REQ_SET_INTERFACE:    handle_set_interface();     break;
            case REQ_GET_INTERFACE:    handle_get_interface();     break;
            case REQ_SET_ADDRESS:      reply_zero_length();        break;
            default:                   reply_stall();              break;
        }
    } else if (reqtype == 0x20) {
        /* Class request — recipient determines handler */
        if (recip == 0x02) {          /* endpoint recipient */
            handle_class_endpoint_request();
        } else if (recip == 0x01) {   /* interface recipient */
            /* TODO: feature-unit volume/mute if we add them */
            reply_stall();
        } else {
            reply_stall();
        }
    } else {
        reply_stall();
    }
}


/* --- Public entry points --- */

void usb_init(void)
{
    /* Reset EP0 config. Rev 20 writes 0xFA to IEPCNF0 (bit 7 = valid,
     * bit 5 = data-toggle-init, bits 3-0 = max-packet-size lo nibble
     * for 8-byte MPS). Our custom firmware follows the same convention. */
    IEPCNF0    = 0xFA;
    IEPBCTX0   = 0;
    OEPCNF0    = 0xFA;
    OEPBCTX0   = 0;

    /* Streaming endpoints stay dormant until SET_INTERFACE(alt=1). */
    IEPCNF1    = 0;
    OEPCNF2    = 0;

    g_configured   = 0;
    g_alt_playback = 0;
    g_alt_capture  = 0;
}

void usb_service(void)
{
    /* Poll for SETUP-received flag. On TAS1020A, that's a bit in a status
     * register (Rev 20 tests 0x24.0 in RAM which mirrors the hardware bit).
     * Full plumbing pending — for now this is where handle_setup() will
     * fire once we've located the exact status bit + interrupt path. */
    /* TODO: check EP0 setup interrupt flag, then: handle_setup(); */
    (void)handle_setup;
    (void)g_ep0_reply_src;
    (void)g_ep0_reply_remaining;
}
