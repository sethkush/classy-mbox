/*
 * Safety-net firmware for Mbox 1.
 *
 * PURPOSE: prove that the flash toolchain + our EEPROM invalidation
 * driver work on real hardware. NOTHING ELSE. No audio, no CS8427,
 * no codec, no mux, no timers, no interrupts. Just enough USB engine
 * to answer the Digi DFU class request (bmReq=0x21, bReq=0x00,
 * wValue=0x000A) and drop the device into boot-ROM DFU mode.
 *
 * Deploy this FIRST, before flashing full mboxfw. If safety_net flashes
 * clean and `mboxflash --enter-dfu` successfully drops it to
 * 0xFFFF:0xFFFE, you have a proven recovery path — subsequent mboxfw
 * flashes can hang and you can still get back with a single command.
 *
 * If safety_net itself bricks, the flash toolchain or the EEPROM
 * invalidation path is broken and needs fixing before ANY mboxfw work.
 *
 * Code budget: aim for under 1 KB so we can visually eyeball the
 * generated assembly and be certain nothing surprising is happening.
 */

#include <mcs51/8051.h>

/* Just the SFRs we touch — no dependency on mboxfw/include/regs.h so
 * this firmware audits standalone. */
#define XDATA(a) (*(volatile __xdata unsigned char *)(a))

#define IEPCNF0     XDATA(0xFF68)
#define IEPBBAX0    XDATA(0xFF69)
#define IEPBSIZ0    XDATA(0xFF6A)
#define IEPBCTX0    XDATA(0xFF6B)
#define OEPCNF0     XDATA(0xFFA8)
#define OEPBBAX0    XDATA(0xFFA9)
#define OEPBSIZ0    XDATA(0xFFAA)
#define OEPBCTX0    XDATA(0xFFAB)
#define MEMCFG      XDATA(0xFFB0)
#define VECINT      XDATA(0xFFB2)
#define USBCTL      XDATA(0xFFFC)
#define USBIMSK     XDATA(0xFFFD)
#define USBFADR     XDATA(0xFFFF)

#define SETPACK_BMREQ  XDATA(0xFF28)
#define SETPACK_BREQ   XDATA(0xFF29)
#define SETPACK_WVAL_L XDATA(0xFF2A)
#define SETPACK_WVAL_H XDATA(0xFF2B)

/* I²C peripheral — same bit map audited in the main mboxfw regs.h. */
#define I2C_STA     XDATA(0xFFC0)
#define I2C_TX      XDATA(0xFFC1)
#define I2C_SADDR   XDATA(0xFFC3)
#define I2C_STOP_WRITE      0x01
#define I2C_XMIT_DATA_EMPTY 0x08
#define I2C_ERROR           0x20
#define I2C_CLEAR_ALL       0x54

/* VECINT interrupt codes */
#define VEC_OEP0   0x00
#define VEC_IEP0   0x08
#define VEC_SETUP  0x12
#define VEC_RSTR   0x17
#define VEC_NONE   0x24

/* USBCTL bits */
#define USBCTL_CONN 0x80

/* EP0 buffer window in TAS1020A shared mem — same convention as mboxfw. */
#define EP0_OUT_BUF_ADDR 0xFA10
#define EP0_IN_BUF_ADDR  0xFA18
#define EP_BBAX(a)       (unsigned char)(((a) - 0xF800) >> 3)
#define EP_BSIZE(b)      (unsigned char)((b) >> 3)


/* --- Minimal descriptors -------------------------------------------
 * VID/PID/bcdDevice chosen to be immediately recognizable in ioreg as
 * "safety net, not mboxfw":
 *   VID=0x0DBA (Digi — so autodetect still works)
 *   PID=0x1000 (audio slot, but we never actually stream)
 *   bcdDevice=0xDEAD (obvious "hey this is the safety net" marker)
 */
const unsigned char __code DevDesc[18] = {
    18, 0x01, 0x10, 0x01, 0x00, 0x00, 0x00, 8,
    0xBA, 0x0D, 0x00, 0x10, 0xAD, 0xDE, 1, 2, 0, 1,
};

/* One config, one interface (bInterfaceClass=0xFE DFU so hosts know we
 * accept DFU class requests), no endpoints other than EP0. */
const unsigned char __code ConfigDesc[18] = {
    /* Config */
    9, 0x02, 18, 0, 1, 1, 0, 0x80, 250,
    /* Interface 0 — DFU class */
    9, 0x04, 0, 0, 0, 0xFE, 0x01, 0x00, 0,
};

const unsigned char __code StringLang[4] = { 4, 0x03, 0x09, 0x04 };
const unsigned char __code StringMfr[]   = {
    18, 0x03, 'D',0,'i',0,'g',0,'i',0,'S',0,'a',0,'f',0,'e',0
};
const unsigned char __code StringProduct[] = {
    18, 0x03, 'M',0,'B',0,'O',0,'X',0,'-',0,'S',0,'A',0,'F',0
};


/* --- Bounded I²C poll (ERROR-aware, matches mboxfw's port of TI I2c.c) */
static unsigned char wait_bit(unsigned char mask)
{
    unsigned int t;
    unsigned char s;
    for (t = 0; t < 0xFF00; t++) {
        s = I2C_STA;
        if (s & I2C_ERROR) return 0;
        if (s & mask)      return 1;
    }
    return 0;
}

static void write_hold(void)
{
    unsigned int i;
    for (i = 0; i < 0xFF00; i++) { }
}

/* Write one byte to EEPROM. Same sequence as mboxfw/src/eeprom.c —
 * intentional duplication so this firmware is standalone-verifiable. */
static unsigned char eeprom_wr(unsigned char hi, unsigned char lo,
                               unsigned char v)
{
    I2C_STA  &= I2C_CLEAR_ALL;
    I2C_SADDR = 0xA0;
    I2C_TX = hi;
    if (!wait_bit(I2C_XMIT_DATA_EMPTY)) return 0;
    I2C_TX = lo;
    if (!wait_bit(I2C_XMIT_DATA_EMPTY)) return 0;
    I2C_STA |= I2C_STOP_WRITE;
    I2C_TX = v;
    if (!wait_bit(I2C_XMIT_DATA_EMPTY)) return 0;
    I2C_STA &= I2C_CLEAR_ALL;
    write_hold();
    return 1;
}


/* --- USB EP0 SETUP handling ---------------------------------------- */

static __data unsigned char pending_addr = 0xFF;

static void reply_zlp(void) { IEPBCTX0 = 0; }
static void stall(void)     { IEPCNF0 &= 0xD7; OEPCNF0 &= 0xD7; }

static void reply_desc(const __code unsigned char *src, unsigned int len)
{
    unsigned int want = ((unsigned int)XDATA(0xFF2F) << 8) | XDATA(0xFF2E);
    unsigned int n = (len > want) ? want : len;
    unsigned int i;
    __xdata unsigned char *dst = (__xdata unsigned char *)EP0_IN_BUF_ADDR;
    if (n > 8) n = 8;   /* clamp to EP0 max packet; only care about first
                         * packet's contents in the safety net */
    for (i = 0; i < n; i++) dst[i] = src[i];
    IEPBCTX0 = (unsigned char)n;
}

static void handle_dfu_trigger(void)
{
    reply_zlp();
    /* Delay for the status-stage to complete on the wire. */
    { unsigned int i; for (i = 0; i < 0xC000; i++) { } }
    /* Best-effort invalidate: write 0x00 over signature bytes at
     * EEPROM offset 2 and 3. If either write fails, we still ljmp 0
     * — same state as before, no worse. */
    (void)eeprom_wr(0x00, 0x02, 0x00);
    (void)eeprom_wr(0x00, 0x03, 0x00);
    __asm__("ljmp 0");
}

static void handle_setup(void)
{
    unsigned char bmReq = SETPACK_BMREQ;
    unsigned char bReq  = SETPACK_BREQ;
    unsigned char wVL   = SETPACK_WVAL_L;
    unsigned char wVH   = SETPACK_WVAL_H;

    /* Digi DFU class trigger — highest priority path. */
    if (bmReq == 0x21 && bReq == 0x00 && wVL == 0x0A && wVH == 0x00) {
        handle_dfu_trigger();
        return;
    }

    /* Standard requests we need to complete enumeration. */
    if ((bmReq & 0x60) == 0x00) {
        switch (bReq) {
            case 0x05: /* SET_ADDRESS */
                pending_addr = wVL;
                reply_zlp();
                return;
            case 0x06: /* GET_DESCRIPTOR */
                switch (wVH) {
                    case 0x01: reply_desc(DevDesc, 18);       return;
                    case 0x02: reply_desc(ConfigDesc, 18);    return;
                    case 0x03:
                        switch (wVL) {
                            case 0: reply_desc(StringLang, 4);            return;
                            case 1: reply_desc(StringMfr, StringMfr[0]);  return;
                            case 2: reply_desc(StringProduct, StringProduct[0]); return;
                        }
                        break;
                }
                break;
            case 0x09: /* SET_CONFIGURATION */
                reply_zlp();
                return;
            case 0x0B: /* SET_INTERFACE */
                reply_zlp();
                return;
        }
    }
    stall();
}


/* --- Polling service loop ------------------------------------------ */

static void usb_service(void)
{
    unsigned char v = VECINT;
    switch (v) {
        case VEC_SETUP:
            handle_setup();
            VECINT = 0;
            break;
        case VEC_IEP0:
            if (pending_addr != 0xFF) {
                USBFADR = pending_addr;
                pending_addr = 0xFF;
            }
            OEPBCTX0 = 0;
            VECINT = 0;
            break;
        case VEC_OEP0:
            VECINT = 0;
            break;
        case VEC_RSTR:
            USBFADR = 0;
            pending_addr = 0xFF;
            VECINT = 0;
            break;
        case VEC_NONE:
        default:
            break;
    }
}


/* --- Main ---------------------------------------------------------- */

void main(void)
{
    /* MEMCFG SDW confirm — boot ROM already set it, be idempotent. */
    MEMCFG |= 0x01;

    /* Bring EP0 up. */
    IEPBBAX0 = EP_BBAX(EP0_IN_BUF_ADDR);
    IEPBSIZ0 = EP_BSIZE(8);
    OEPBBAX0 = EP_BBAX(EP0_OUT_BUF_ADDR);
    OEPBSIZ0 = EP_BSIZE(8);
    IEPBCTX0 = 0x80;   /* NAK first IN token */
    OEPBCTX0 = 0;
    IEPCNF0  = 0x84;
    OEPCNF0  = 0x84;

    USBFADR   = 0;
    USBIMSK  |= 0xE5;      /* SETUP + reset + suspend/resume + STPOW */

    /* Attach to bus — RMW just CONN (Rev-20-style). */
    USBCTL |= USBCTL_CONN;

    /* Poll forever. No interrupts, no ISRs. If we hang, mboxflash
     * --enter-dfu still works because we're already listening. */
    for (;;) {
        usb_service();
    }
}
