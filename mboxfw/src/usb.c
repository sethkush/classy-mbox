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
#include "streaming.h"
#include "eeprom.h"
#include "telemetry.h"

/* LED progress canary — see the ladder in main.c. Stages 10-17 live on
 * the USB side; g_stage is defined there. */
#ifdef CANARY_LED
extern volatile __data unsigned char g_stage;
/* Last bRequest seen in a SETUP, and a count of stalls issued. The stage
 * ladder is a monotonic maximum, so it cannot say WHICH request is
 * failing — only how far the best case got. These two are reported as
 * separate blink groups after the stage. */
extern volatile __data unsigned char g_last_breq;
extern volatile __data unsigned char g_stalls;
/* EP0 IN packets pushed since the last SETUP. The config descriptor is
 * 180 bytes = 23 packets; the device descriptor is 18 = 3. Reporting the
 * count distinguishes "the long reply truncated" from "the long reply was
 * never requested", which the monotonic stage ladder cannot. */
extern volatile __data unsigned char g_chunks;
#define STAGE(n) do { if ((unsigned char)(n) > g_stage) g_stage = (n); } while (0)
#else
/* Non-canary build: the stage ladder still feeds telemetry block 0, so the
 * high-water mark is readable over USB without the LED diagnostic build. */
#define STAGE(n) do { if ((unsigned char)(n) > tlm_stage) tlm_stage = (n); } while (0)
#endif

/* Descriptor tables from descriptors.c */
extern const __code unsigned char AppDevDesc[];
extern const __code unsigned char AppConfigDesc[];
extern const __code unsigned char AppStringLang[];
extern const __code unsigned char AppStringMfr[];
extern const __code unsigned char AppStringProduct[];

/* Enter-DFU request latch. Written from ISR context (the SETUP handler),
 * read and cleared by main(); volatile so SDCC cannot cache the main-loop
 * test across iterations. */
volatile __data unsigned char g_dfu_request_pending = 0;

/* Current USB device state — updated by SET_CONFIGURATION / SET_INTERFACE. */
static __data unsigned char g_configured = 0;
static __data unsigned char g_alt_playback = 0;   /* alt setting on interface 1 */
static __data unsigned char g_alt_capture  = 0;   /* alt setting on interface 2 */
static __data unsigned long g_sample_rate  = 48000UL;   /* 24-bit BE on the wire */

/* Pending USB device address, deferred until the SET_ADDRESS status
 * stage has actually been ACKed by the host. Writing USBFADR too early
 * makes the ACK go out at the new address and the host rejects it —
 * enumeration then wedges after SET_ADDRESS with the device visible at
 * VID only, no PID/bcdDevice (empirically bricked mboxfw v1 the same
 * way on 2026-07-18). 0xFF = no pending assignment. */
static __data unsigned char g_pending_address = 0xFF;

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
#ifdef CANARY_LED
    if (g_stalls < 99) g_stalls++;
#endif
    /* STALL is bit 3; it must be OR'd IN, not masked out.
     *
     * This previously read `IEPCNF0 &= 0xD7`, which CLEARS bit 5 (TOGGLE)
     * and bit 3 (STALL) — the exact opposite of stalling. The endpoint
     * was left un-stalled with its toggle reset, so an unsupported
     * request got no handshake at all and the host simply timed out.
     * safety_net carried the identical bug and fixed it; porting that
     * fix here. Reference: TI hwMacro.h:9-10, STALLInEp0/STALLOutEp0,
     * both `|= 0x08`. */
    TLM_INC8(tlm_stalls);
    IEPCNF0 |= 0x08;
    OEPCNF0 |= 0x08;
}

static void reply_zero_length(void)
{
    /* Drop any half-finished multi-packet reply before arming the status
     * stage. stage_immediate() already does this (see its
     * g_ep0_reply_remaining = 0); this path did not, which is the EP0
     * desync fixed in safety_net on 2026-07-26 and proven on hardware.
     * See the flush at the top of handle_setup() for the full story. */
    g_ep0_reply_remaining = 0;
    /* Zero-length IN packet acknowledges an OUT-only request (SET_CONFIG
     * etc.). TI UsbEng.c engEp0SetupDone takes the same path for the
     * wLength == 0 case; Rev 20's EP0 IN loader fcn.0x0B8C writes the
     * byte count to this same register. */
    IEPBCTX0 = 0;
}

/* Stage a small (up to EP0_MAX_PACKET bytes) reply from RAM directly
 * into the EP0 IN buffer. Used for one-shot generated replies —
 * GET_INTERFACE returning the current alt setting, UAC_GET_CUR
 * returning the sample-rate triple. Callers with larger or __code-
 * resident payloads should use stage_reply() instead. */
static void stage_immediate(const unsigned char *bytes, unsigned char len)
{
    __xdata unsigned char *dst = (__xdata unsigned char *)EP0_IN_BUF_ADDR;
    unsigned char n = (len > EP0_MAX_PACKET) ? EP0_MAX_PACKET : len;
    unsigned char i;
    /* Cap to what the host asked for (wLength) — never send more. */
    unsigned int wLen = ((unsigned int)wLenH << 8) | wLenL;
    if (n > wLen) n = (unsigned char)wLen;
    for (i = 0; i < n; i++) {
        dst[i] = bytes[i];
    }
    g_ep0_reply_remaining = 0;   /* single packet, no continuation */
    IEPBCTX0 = n;
}

/* Push up to EP0_MAX_PACKET bytes from g_ep0_reply_src into the EP0 IN
 * buffer at 0xFA10, arm IEPBCTX0 with the byte count, and advance the
 * cursor. Called first by stage_reply() and then by usb_service() on
 * each IEP0-done interrupt until g_ep0_reply_remaining hits zero. */
static void push_reply_chunk(void)
{
#ifdef CANARY_LED
    if (g_chunks < 99) g_chunks++;
#endif
    /* 18 = a chunk was pushed; 19 = the transfer drained completely.
     * Together these show how far into a long multi-packet reply we get
     * before stalling. */
    STAGE(18);
    __xdata unsigned char *dst = (__xdata unsigned char *)EP0_IN_BUF_ADDR;
    unsigned char n = (g_ep0_reply_remaining > EP0_MAX_PACKET)
                          ? EP0_MAX_PACKET
                          : (unsigned char)g_ep0_reply_remaining;
    unsigned char i;
    for (i = 0; i < n; i++) {
        dst[i] = g_ep0_reply_src[i];
    }
    g_ep0_reply_src       += n;
    g_ep0_reply_remaining -= n;
    TLM_INC16(tlm_chunks);
    if (g_ep0_reply_remaining == 0) { STAGE(19); TLM_INC16(tlm_drains); }
    IEPBCTX0 = n;   /* hand the packet to the hardware */
}

static void stage_reply(__code const unsigned char *src, unsigned int len)
{
    /* Cap reply length to what the host actually asked for. */
    unsigned int wLen = ((unsigned int)wLenH << 8) | wLenL;
    if (len > wLen) len = wLen;
    g_ep0_reply_src = src;
    g_ep0_reply_remaining = len;
    push_reply_chunk();   /* first chunk fires immediately */
}


/* --- Standard request dispatchers --- */

static void handle_get_descriptor(void)
{
    unsigned char type  = wValueH;
    unsigned char index = wValueL;

    /* LENGTHS ARE COMPILE-TIME CONSTANTS — never a runtime `Arr[0]` read.
     *
     * safety_net used reply_desc(StringMfr, StringMfr[0]) and delivered
     * only 16 of 18 bytes, then went silent. Host probe 2026-07-27
     * (mboxflash --descdump) against the running firmware:
     *
     *   wLength  2 ->  2 OK      wLength 17 -> TIMEOUT
     *   wLength  9 ->  9 OK      wLength 18 -> TIMEOUT
     *   wLength 16 -> 16 OK      wLength 255-> TIMEOUT
     *
     * i.e. the reply was capped at 16 bytes, and because 16 is a whole
     * number of 8-byte packets there is no short packet to terminate the
     * transfer, so the host waits forever. The same firmware's device and
     * config descriptors — called with a literal length — returned all 18
     * bytes correctly at every wLength. Reproducible with the request
     * issued first on a freshly opened device, so not stale EP0 state.
     *
     * The arrays are correct (link map confirms the sizes, and the device
     * sends the right bLength byte on the wire). The symptom is proven;
     * the SDCC mechanism behind the runtime read is not. Use the call
     * pattern that demonstrably works.
     *
     * mboxfw had this on the DEVICE descriptor too, which would break
     * enumeration at the very first request. */
    switch (type) {
        case USB_DT_DEVICE:
            STAGE(14);
            stage_reply(AppDevDesc, APP_DEV_DESC_LEN);
            break;
        case USB_DT_CONFIG:
            /* 17 = config descriptor requested. This is the big one:
             * 180 bytes = 23 EP0 packets, versus 18 bytes / 3 packets for
             * the device descriptor. safety_net never had a descriptor
             * anywhere near this size, so the deep continuation path has
             * never actually been exercised on hardware. */
            STAGE(17);
            stage_reply(AppConfigDesc, APP_CFG_TOTAL_LEN);
            break;
        case USB_DT_STRING:
            switch (index) {
                case 0:  stage_reply(AppStringLang,    APP_STRING_LANG_LEN);  break;
                case 1:  stage_reply(AppStringMfr,     APP_STRING_MFR_LEN); break;
                case 2:  stage_reply(AppStringProduct, APP_STRING_PRODUCT_LEN); break;
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
    STAGE(20);   /* enumeration essentially complete */
    g_configured = wValueL;
    reply_zero_length();
}

static void handle_set_interface(void)
{
    unsigned char iface = wIndexL;
    unsigned char alt   = wValueL;

    if (iface == 1) {
        g_alt_playback = alt;
        streaming_playback_enable(alt != 0);
    } else if (iface == 2) {
        g_alt_capture = alt;
        streaming_capture_enable(alt != 0);
    }
    reply_zero_length();
}

static void handle_get_interface(void)
{
    unsigned char iface = wIndexL;
    unsigned char alt_reply;

    if (iface == 1)      alt_reply = g_alt_playback;
    else if (iface == 2) alt_reply = g_alt_capture;
    else                 alt_reply = 0;
    stage_immediate(&alt_reply, 1);
}


/* --- Digi custom class request (enter-DFU) --- */
/*
 * Rev 20 responds to  bmReqType=0x21 bReq=0x00 wValue=0x000A wLength=0
 * (host→device / class / interface, sent to interface 0). This is the
 * "software DFU trigger" — how mboxflash --enter-dfu asks a running
 * device to reset back into boot-ROM DFU mode so it can be reflashed.
 *
 * Getting this right is a HARD requirement for mboxfw v1: without it,
 * a soft-brick can only be recovered by physically opening the Mbox
 * and shorting the EEPROM's SDA line to GND during power-up (learned
 * the hard way on 2026-07-18). Any firmware that ships without it is
 * a one-way ticket.
 *
 * Approach in this version: acknowledge the request with a zero-length
 * IN status, then jump to the reset vector. On many TAS1020A designs
 * the warm-reset path re-checks the boot mode; if it doesn't, this at
 * least gets the CPU into a known state instead of a hung handler.
 *
 * TRUE bulletproof recovery requires invalidating the EEPROM header
 * signature bytes (offset 2-3, value 0x12 0x34) via the hardware-I²C
 * peripheral at 0xFFC0-0xFFC3 before the reset — the boot ROM sees a
 * bad signature and drops to its own DFU mode (0xFFFF:0xFFFE). That's
 * a follow-up TODO — it needs I²C-EEPROM-write code we don't have yet
 * and I don't want to add un-testable code paths without hardware access.
 * Once added, a successful flash restores the signature bytes.
 */
/* The Digi enter-DFU class request: ACK now, invalidate the EEPROM
 * signature from the main loop, then halt. The user's next power cycle
 * lands in boot-ROM DFU.
 *
 * There is NO software path from a running image back to the boot ROM on
 * this part, so this handler cannot "reset into DFU":
 *
 *   - MEMCFG.SDW must be cleared to remap the boot ROM at 0x0000, but any
 *     code clearing it from program RAM unmaps ITSELF mid-routine. TI's
 *     UtilResetBootCPU is immune only because Utils.SRC links it at 0x8003,
 *     inside the ROM (see the warning on RESET_TO_BOOT_ROM in regs.h).
 *   - USBCTL.FRSTE does not help: the datasheet states a USB-reset function
 *     reset leaves "the shadow the ROM (SDW) and the USB function connect
 *     (CONT) bits" untouched, so the MCU restarts into this same image.
 *   - Stock Rev 20 has no such path either. Its only MEMCFG write is at
 *     0x08E6 and SETS SDW; there is no `ljmp 0x8000` anywhere in the image.
 *
 * So the operation is: zero the signature, halt, let the user replug. That
 * is exactly the sequence sigkill performed from RAM on 2026-07-28, which
 * is the only proof on record that these I2C writes reach the part.
 *
 * DEFERRED to the main loop, deliberately. Four eeprom_wr() calls are ~30 ms
 * of program-cycle waits; running them here would block EP0 through the
 * status stage and past the host's timeout, so the host would record the
 * request as failed even though it took effect. Setting a flag lets the
 * zero-length status packet drain first. This is NOT the discredited
 * 2026-07-27 "move it out of the ISR" fix — that one deferred a
 * RESET_TO_BOOT_ROM() which could never work at any priority level. What is
 * deferred here is an I2C write, which works fine from the main loop.
 *
 * Shipping mboxfw without this on 2026-07-28 left a running image with no
 * escape hatch and cost a physical SDA short to recover (BRICK_LOG). */
static void handle_digi_enter_dfu(void)
{
    reply_zero_length();
    g_dfu_request_pending = 1;
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
        /* Host sending 3-byte sample rate as 24-bit LE (UAC1 §5.2.2.1.1).
         * Rev 20 cheats by reading only src[0] — works for 44100/48000
         * because their low bytes (0x44/0x80) are unique, but silently
         * accepts nonsense like 300 kHz. We parse the full 24-bit value
         * and STALL anything not in our supported rate list so the host
         * gets an unambiguous "no" rather than a broken pipe. */
        __xdata unsigned char *src = (__xdata unsigned char *)EP0_OUT_BUF_ADDR;
        unsigned long rate = (unsigned long)src[0]
                           | ((unsigned long)src[1] << 8)
                           | ((unsigned long)src[2] << 16);
        if (rate == 44100UL || rate == 48000UL) {
            g_sample_rate = rate;
            streaming_set_rate(rate);
            reply_zero_length();
        } else {
            reply_stall();
        }
    } else if (bReq == UAC_GET_CUR) {
        /* Reply with current sample rate as 3-byte LE (UAC1 §5.2.2.1.1). */
        unsigned char rate_bytes[3];
        rate_bytes[0] = g_sample_rate & 0xFF;
        rate_bytes[1] = (g_sample_rate >> 8) & 0xFF;
        rate_bytes[2] = (g_sample_rate >> 16) & 0xFF;
        stage_immediate(rate_bytes, 3);
    } else {
        reply_stall();
    }
}


/* --- Top-level SETUP dispatch (mirrors Rev 20 fcn.0x00 @ 0x0026) --- */

static void handle_setup(void)
{
    unsigned char reqtype = bmReq & 0x60;   /* mask off type field */
    unsigned char recip   = bmReq & 0x1F;
#ifdef CANARY_LED
    g_last_breq = bReq;
#endif

    /* EP0 SETUP PROLOGUE — ported from safety_net, which is proven to
     * enumerate on this hardware. mboxfw had none of this.
     *
     * TI engEp0SetupDone (UsbEng.c:223-228) opens with exactly these two
     * pairs: STALLClrInEp0/STALLClrOutEp0 then TOGGLEInEp0Data/
     * TOGGLEOutEp0Data (hwMacro.h:27-28, 46-47).
     *
     * STALL-clear: once any unsupported request has stalled EP0, the
     * STALL bit stays set and every later response stalls too, wedging
     * enumeration permanently.
     *
     * TOGGLE=1: the first data or status packet of a control transfer
     * must be DATA1 (datasheet p.30, Control Read Setup step 2). The
     * bus-reset handler sets IEPCNF0 = 0x84, which leaves TOGGLE (bit 5)
     * CLEAR — so without this every post-reset reply goes out as DATA0,
     * the host discards it, retries, and gives up. Silent device.
     *
     * safety_net's comment records these two lines together fixing
     * exactly the "flash succeeds, firmware runs, nothing on USB"
     * symptom on 2026-07-23. mboxfw shows the same symptom: canary
     * stage 16, last bRequest 5 (SET_ADDRESS), zero stalls, no
     * descriptor ever staged. */
    /* TI UsbEng.c engEp0SetupDone — STALLClrInEp0 / STALLClrOutEp0 */
    IEPCNF0 &= (unsigned char)~0x08;
    OEPCNF0 &= (unsigned char)~0x08;
    /* TI UsbEng.c engEp0SetupDone — TOGGLEInEp0Data / TOGGLEOutEp0Data */
    IEPCNF0 |= 0x20;
    OEPCNF0 |= 0x20;
#ifdef CANARY_LED
    g_chunks = 0;      /* per-transfer count */
#endif

    /* ABANDON ANY IN-FLIGHT CONTROL TRANSFER.
     *
     * USB 2.0 §8.5.3: a SETUP packet always terminates whatever control
     * transfer was in progress. TI does this via EMPTYInEp0 /
     * EMPTYOutEp0 (hwMacro.h:50,53) at the head of engEp0SetupDone
     * (UsbEng.c:230-231).
     *
     * Without it: macOS requests the device descriptor with wLength=64,
     * we clamp to 18 and ship 8, leaving g_ep0_reply_remaining = 10.
     * macOS only wanted bMaxPacketSize0, so it resets and sends
     * SET_ADDRESS. reply_zero_length() arms the status ZLP but left the
     * counter set, so the VEC_IEP0 completion wrote USBFADR and then
     * shipped 8 leftover descriptor bytes AS the address status stage.
     * EP0 desynchronises; the host keeps talking but every subsequent
     * transfer is corrupt, and enumeration never completes.
     *
     * Found in safety_net by the LED canary (stages 12→13→14 then stuck)
     * and fixed there 2026-07-26; safety_net enumerated immediately
     * afterwards, bcdDevice 0xDEAD visible on the bus. mboxfw carried the
     * identical asymmetry — stage_immediate() cleared the counter,
     * reply_zero_length() did not — so it is fixed here by the same
     * means. See safety_net/EP0_DIFF_vs_REV20.md §5. */
    g_ep0_reply_remaining = 0;

    /* Record the SETUP before dispatching, so a request that wedges us is
     * still visible in telemetry block 2 afterwards. */
    tlm_last_bmreq   = bmReq;
    tlm_last_breq    = bReq;
    tlm_last_wvalue  = ((unsigned int)wValueH << 8) | wValueL;
    tlm_last_windex  = ((unsigned int)wIndexH << 8) | wIndexL;
    tlm_last_wlength = ((unsigned int)wLenH   << 8) | wLenL;

    /* Vendor requests (telemetry) are handled first and unconditionally.
     * They must keep working even when everything else is broken — that is
     * their entire purpose. DEVICE recipient, so no interface claim is
     * needed and snd-usb-audio cannot intercept them. */
    if (reqtype == 0x40) {
        if (bReq == TLM_REQ_READ && (bmReq & 0x80)) {
            unsigned char blk[TLM_BLOCK_SIZE];
            (void)tlm_read_block(wValueL, blk);
            /* Always exactly one 8-byte packet — never the multi-packet
             * continuation path, which is the thing under investigation. */
            stage_immediate(blk, TLM_BLOCK_SIZE);
        } else if (bReq == TLM_REQ_RESET && !(bmReq & 0x80)) {
            tlm_reset_counters();
            reply_zero_length();
        } else {
            reply_stall();
        }
        return;
    }

    if (reqtype == 0x00) {
        /* Standard request */
        switch (bReq) {
            case REQ_GET_DESCRIPTOR:   handle_get_descriptor();   break;
            case REQ_SET_CONFIG:       handle_set_configuration(); break;
            case REQ_SET_INTERFACE:    handle_set_interface();     break;
            case REQ_GET_INTERFACE:    handle_get_interface();     break;
            case REQ_SET_ADDRESS:
                /* USB 2.0 §9.4.6: the new address takes effect only after
                 * the STATUS stage (zero-length IN packet) completes. Stage
                 * that reply here and defer the USBFADR write to the
                 * VEC_IEP0 completion in usb_service() below. */
                STAGE(15);
                g_pending_address = wValueL;
                reply_zero_length();
                break;
            case REQ_GET_STATUS: {
                /* USB 2.0 §9.4.5: 2 bytes for every recipient. Bus-powered,
                 * no remote wakeup, no halt support => both bytes zero.
                 * macOS IOUSBFamily issues this during enumeration, so it
                 * MUST be answered. */
                unsigned char st[2];
                st[0] = 0; st[1] = 0;
                stage_immediate(st, 2);
                break;
            }
            case REQ_CLEAR_FEATURE:
            case REQ_SET_FEATURE:
                /* No features are supported (no remote wakeup, and the iso
                 * streaming endpoints cannot halt per USB 2.0 §5.6.4).
                 * Acknowledge rather than stall — a stall here makes some
                 * hosts abandon the device. */
                reply_zero_length();
                break;
            case REQ_GET_CONFIG:
                stage_immediate(&g_configured, 1);
                break;
            default:
                /* Everything else (SET_DESCRIPTOR, SYNCH_FRAME, reserved
                 * codes) is optional: STALL is the spec-correct response.
                 *
                 * DO NOT reintroduce `lcall #0x2f00` here. That was present
                 * until 2026-07-26, justified by a comment claiming "Rev 20
                 * relies on the same fallback (its 0x0118 shim ends in ljmp
                 * 0x2f00)". That claim was fabricated. The bytes 02 2F 00 at
                 * Rev 20 0x011F are not an instruction at all — they are the
                 * first entry of its standard-request dispatch table
                 * (BE16 handler 0x022F, bRequest 0x00 = GET_STATUS), which
                 * runs to 0x0140 and covers all 11 standard requests
                 * in-firmware. Rev 20 never delegates to the boot ROM.
                 * Verified by decoding firmware_stock/rev20_firmware_code.bin
                 * directly; see firmware_stock/disasm/rev20_ANNOTATED.md.
                 *
                 * On mboxfw the call was fatal: our image ends at 0x0C7D, so
                 * 0x2F00 is ~8.8 KB past it. Any standard request that fell
                 * through to the default case executed unmapped memory and
                 * killed the CPU mid-enumeration. */
                reply_stall();
                break;
        }
    } else if (reqtype == 0x20) {
        /* Class request — recipient determines handler */
        if (recip == 0x02) {          /* endpoint recipient */
            handle_class_endpoint_request();
        } else if (recip == 0x01 || recip == 0x00) {  /* interface OR device */
            /* Digi custom enter-DFU: bReq=0x00, wValue=0x000A.
             *
             * Accept recipient = device (0x00) as well as interface
             * (0x01), and do not require wIndex == 0.
             *
             * The DFU trigger is the ONLY escape from a soft-brick that
             * does not involve opening the case and shorting EEPROM SDA
             * to ground during power-up. Being strict here buys nothing
             * and costs a disassembly session with a screwdriver, so the
             * match is deliberately permissive: any class request with
             * bReq=0 and wValue=0x000A means "get me into DFU".
             *
             * 2026-07-27: --enter-dfu against enumerated safety_net gets
             * kIOUSBPipeStalled and the device keeps running, so the
             * handler never fired. Unresolved whether macOS declines to
             * send an interface-recipient request to a device whose
             * interfaces it has not published, or the firmware sees it
             * and falls through. Widening the match removes one of those
             * possibilities for free. Rev 20 and the Digidesign updater
             * use 0x21/wIndex 0, which still matches. */
            if (bReq == 0x00 && wValueL == 0x0A && wValueH == 0x00) {
                handle_digi_enter_dfu();
            } else {
                /* TODO: feature-unit volume/mute if we add them */
                reply_stall();
            }
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
    /* Point EP0 IN / OUT at their packet buffers in the shared window. */
    IEPBBAX0 = EP_BBAX(EP0_IN_BUF_ADDR);
    IEPBSIZ0 = EP_BSIZE(EP0_MAX_PACKET);
    OEPBBAX0 = EP_BBAX(EP0_OUT_BUF_ADDR);
    OEPBSIZ0 = EP_BSIZE(EP0_MAX_PACKET);

    /* Enable EP0. Rev 20's reset init (rev20_flat.asm @ 0x099e/0x09a7)
     * writes 0x84 to IEPCNF0 (0xFFA8) and OEPCNF0 (0xFF68) — bit 7 =
     * USBIE (enable) + bit 2 (interrupt-on-transaction). Earlier drafts
     * of this file wrote 0xFA which was a guess; 0x84 is what the
     * shipping vendor firmware uses. */
    /* IEPBCTX0 top bit is the NAK flag. TI's engUsbInit starts EP0 IN
     * in NAK state (0x80) so the first IN token doesn't ship a spurious
     * zero-length packet before we have data to send. Ours previously
     * set 0 → could confuse strict hosts. */
    IEPBCTX0 = 0x80;
    OEPBCTX0 = 0;
    IEPCNF0  = 0x84;
    OEPCNF0  = 0x84;

    /* Streaming endpoints stay dormant until SET_INTERFACE(alt=1). */
    IEPCNF1 = 0;
    OEPCNF2 = 0;

    /* Reset USB function address (host will re-assign via SET_ADDRESS). */
    USBFADR = 0;

    /* Unmask USB interrupt sources. TI's engUsbInit uses 0xE5:
     *   bit 0 (0x01) STPOW  — setup overwrite
     *   bit 2 (0x04) SETUP  — SETUP packet received
     *   bit 5 (0x20) SUSR   — suspend
     *   bit 6 (0x40) RESR   — resume
     *   bit 7 (0x80) RSTR   — bus reset
     * Earlier drafts wrote 0xFF (all bits including IEP/OEP done) as an
     * assignment; that also clobbered whatever boot ROM had set. RMW to
     * be safe. Reference: TI UsbEng.c::engUsbInit line ~647.
     *
     * NOTE (2026-07-25): Rev 20's boot USBIMSK is actually 0x9F
     * (rev20_flat.asm 0x09FE-0x0A03, verified), NOT 0xE5 as an
     * earlier doc row claimed — 0x091A writes 0xE5 to CPTCNF4
     * (0xFFDF), not USBIMSK. Choosing 0xE5 over Rev 20's 0x9F is a
     * deliberate architectural divergence: we dispatch SETUP via
     * the STPOW interrupt path (bit 5), while Rev 20 uses a polled
     * EP0 model that doesn't need STPOW. Do NOT "fix" this back to
     * 0x9F. */
    USBIMSK |= 0xE5;

    g_configured   = 0;
    g_alt_playback = 0;
    g_alt_capture  = 0;
    g_ep0_reply_remaining = 0;

    /* Settle before attach. Rev 20 (rev20_flat.asm 0x0AC5-0x0AD8) runs a
     * ~65k-iter outer loop between finishing peripheral init and enabling
     * EA / attaching D+. Gives the USB engine time to reach a stable idle
     * state. mboxfw's outer order runs hw_init/cs8427/codec AFTER usb_init
     * so a lot of settle time exists naturally, but the extra ~15 ms loop
     * here matches Rev 20's pattern exactly and is defensive against
     * future ordering changes. */
    { unsigned int settle; for (settle = 0; settle < 0xFFFF; settle++) { } }

    /* NO ATTACH HERE. usb_init() only configures endpoints and buffers.
     * The bus attach lives in usb_attach(), called from main() after all
     * hardware init and after EA=1 — see the comment there. */
}

/*
 * Attach to the USB bus. MUST be called last, after every other init and
 * after EA=1.
 *
 * Both stock firmwares do exactly this, verified by full startup traces:
 *   Rev 20  hardware init -> SETB EA @0x0ACA -> USBCTL |= 0x80 @0x0AD2
 *   Rev 22  hardware init -> SETB EA       -> USBCTL |= 0x80 @0x0A7C
 * Two instructions apart in Rev 20. The device does not present its D+
 * pull-up until it is fully able to answer the host.
 *
 * CONN only (bit 7), not CONN|FEN. Neither stock image contains a single
 * `ORL A,#0x40` — FEN is asserted only from the bus-reset handler
 * (Rev 20 0x0F60 inside the RSTR handler at 0x0F43, VECINT slot 0x17;
 * Rev 22 0x0F81 inside usb_rstr_handler at 0x0F64). Our VEC_RSTR case
 * does the same, so FEN comes up on the first bus reset exactly as the
 * stock firmware arranges it.
 *
 * A CONN-only attach was tried on hardware 2026-07-25 and appeared to
 * fail, which is why this file briefly forced CONN|FEN here. That test
 * was confounded: the build under test still contained the fatal
 * `lcall #0x2f00` in handle_setup, so no configuration could have
 * enumerated. The stock traces are the stronger evidence.
 */
void usb_attach(void)
{
    USBCTL |= USBCTL_CONN;
}

void usb_service(void)
{
    unsigned char vec = VECINT;

    switch (vec) {
        case VEC_NONE:  TLM_INC8(tlm_vec_none);  break;
        default:        break;
    }

    switch (vec) {
        case VEC_SETUP:
            TLM_INC8(tlm_vec_setup);
            TLM_INC16(tlm_setup_count);
            handle_setup();
            /* Firmware-initiated ACK of the SETUP by clearing VECINT. */
            VECINT = 0;
            break;

        case VEC_IEP0:
            /* ACK the vector BEFORE arming the next packet.
             *
             * TI UsbEng.c engEx0():
             *     case IEP0_INT:
             *         VECINT=0;
             *         engEp0TxDone();
             * This code used to ship the chunk first and clear VECINT at
             * the bottom of the case, which loses interrupts: once
             * IEPBCTX0 is written the packet can complete before we reach
             * the clear, and VECINT = 0 then wipes the completion event
             * for the packet just armed. The continuation never fires
             * again and the transfer hangs.
             *
             * Measured 2026-07-27, 60 trials/size: 1-2 packet replies
             * 60/60; 3 packets 53/60; 4 packets 47/60; 8 packets 27/60;
             * 23 packets 9/60 — a clean geometric ~12% loss per
             * continuation past the first. safety_net showed the same
             * curve from the same ordering, which is what ruled out every
             * mboxfw-specific theory (SDCC overlay, busy main loop).
             *
             * Ordering and cleanup otherwise ported from safety_net.
             *
             * USB 2.0 §9.4.6: a pending SET_ADDRESS takes effect only
             * AFTER its zero-length IN status stage completes. VEC_IEP0
             * firing means the host ACKed that packet, so latch USBFADR
             * now and the next transaction lands at the new address. */
            /* TI UsbEng.c engEx0 — `case IEP0_INT: VECINT=0;` precedes
             * engEp0TxDone(), so the vector is acknowledged before the
             * next packet is armed. */
            VECINT = 0;
            TLM_INC8(tlm_vec_iep0);
            TLM_INC16(tlm_iep0_count);
            if (g_pending_address != 0xFF) {
                /* 16 = deferred address actually applied. */
                STAGE(16);
                /* TI UsbEng.c engEp0TxDone — deferred SET_ADDRESS latch;
                 * Rev 20 fcn.0x0F91 @ 0x0FAD writes USBFADR here too. */
                USBFADR = g_pending_address;
                g_pending_address = 0xFF;
            }

            if (g_ep0_reply_remaining) {
                push_reply_chunk();
            } else {
                /* Transfer complete. Rev 20's EP0 cleanup at 0x0B30:
                 * clear TOGGLE + STALL and zero the IN count, priming
                 * EP0 for the next SETUP. mboxfw omitted this entirely,
                 * leaving stale toggle/stall state across transfers. */
                IEPCNF0 &= 0xD7;   /* clear bit 5 TOGGLE + bit 3 STALL */
                OEPCNF0 &= 0xD7;
                IEPBCTX0 = 0;
            }
            /* Re-arm EP0 OUT on EVERY IN completion, not just at end of
             * transfer — safety_net does this unconditionally. */
            OEPBCTX0 = 0;
            break;

        case VEC_OEP0:
            TLM_INC8(tlm_vec_oep0);
            /* Host sent data (or status stage). Nothing to do for the
             * requests we handle so far — just acknowledge. TI UsbEng.c
             * engEx0(): `case OEP0_INT: VECINT=0; engEp0RxDone();` —
             * vector cleared first, same as IEP0 above. */
            VECINT = 0;
            break;

        case VEC_RSTR:
            TLM_INC8(tlm_vec_rstr);
            TLM_INC16(tlm_rstr_count);
            /* USB bus reset. Per TAS1020B datasheet §6.5.1.4, bus reset
             * CLEARS FEN — the UBM then ignores all USB transactions
             * until FEN is set again. Re-arm EP0 config (also cleared
             * by reset) and re-assert CONN|FEN together.
             *
             * DO NOT call usb_init() here — that would re-run the 65k
             * settle loop inside an ISR-adjacent context, blow away
             * USBIMSK (RMW OR of 0xE5 is idempotent, but the extra work
             * is pointless), and reset g_pending_address/g_ep0_reply_*
             * state that a mid-enum reset shouldn't clobber. Mirrors
             * safety_net/src/main.c:402-433 exactly. */
            /* TI UsbEng.c engUsbInit: IEPCNF0=OEPCNF0=0x84, USBFADR=0.
             * Rev 20 fcn.0x0F72 @ 0x0F7C-0x0F82: USBCTL |= 0xC0. */
            OEPCNF0 = 0x84;
            IEPCNF0 = 0x84;
            USBFADR = 0;
            USBCTL |= (USBCTL_CONN | USBCTL_FEN);
            g_pending_address = 0xFF;
            g_ep0_reply_remaining = 0;
            g_configured = 0;
            g_alt_playback = 0;
            g_alt_capture = 0;
            VECINT = 0;
            break;

        case VEC_SOF:
            streaming_sof();
            VECINT = 0;
            break;

        case VEC_NONE:
            break;
        default:
            /* Any other unmasked source (STPOW, SUSR, RESR, spurious).
             * Per datasheet §6.5.7.3, VECINT must be written to clear
             * the source; without it the ISR re-fires on return and
             * the CPU wedges. TI's usbIntrHandler (UsbEng.c:44-96)
             * writes VECINT = 0 in every case for this reason. */
            VECINT = 0;
            break;
    }
}
