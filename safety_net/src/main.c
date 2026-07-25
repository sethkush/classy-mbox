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

#define GLOBCTL     XDATA(0xFFB1)
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

/* USBCTL attach bit — CONN (D+ pullup). Rev 20 (rev20_flat.asm 0x0ADE)
 * does `USBCTL |= 0x80` from a known-0 base, final value 0x80 (CONN
 * only, FEN cleared). safety_net previously assigned 0xC0 (matching
 * TI reference UsbEng.c:647) but Flash #2 in BRICK_LOG.md is exactly
 * the bug where assigning USBCTL clobbers boot-ROM state; POLICY §2
 * mandates RMW-only for boot-ROM-owned SFRs; the diff-justifications
 * doc rows 320-322 explicitly cite Rev 20's `|= 0x80` as correct.
 * Additionally, FEN=1 makes USB bus resets trigger a CPU global reset
 * (datasheet §4.9), which would make our VEC_RSTR handler dead code.
 * Match Rev 20 which works: OR bit 7 only. */
#define USBCTL_ATTACH_BIT 0x80

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

/* One config, one interface. bInterfaceClass = 0xFF (vendor) — NOT 0xFE
 * (DFU). If we claim DFU class, DFU 1.0 §4.1.3 requires a DFU functional
 * descriptor (bDescriptorType=0x21) immediately after this interface
 * descriptor; without it, strict DFU-aware hosts (dfu-util, some macOS
 * probes) reject the config. Rev 20 (real Mbox 1 in Phase-0 dump) uses
 * vendor class 255 for the same reason — it accepts Digi class requests
 * without formally declaring DFU. safety_net's mission is only to
 * (1) enumerate and (2) accept the Digi enter-DFU request
 * (bmReq=0x21, bReq=0x00, wValue=0x000A) which our SETUP dispatcher
 * matches on bmReq/bReq/wValue directly — interface class is irrelevant. */
const unsigned char __code ConfigDesc[18] = {
    /* Config */
    9, 0x02, 18, 0, 1, 1, 0, 0x80, 250,
    /* Interface 0 — vendor class */
    9, 0x04, 0, 0, 0, 0xFF, 0x00, 0x00, 0,
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

/* Multi-packet descriptor continuation state. GET_DESCRIPTOR for Device
 * (18B), Config (18B) or String (18B) all exceed the EP0 8-byte max
 * packet size. Prior version clamped to 8 bytes and stopped — host got
 * a short packet as end-of-transfer, saw an incomplete descriptor, and
 * gave up enumerating (silent device). The IEP0-done handler (VEC_IEP0)
 * now checks `desc_left` and ships the next 8-byte chunk from
 * `desc_ptr` until exhausted. Reference: TI's engEp0TxDone / engLoadTxFifo
 * state-machine (UsbEng.c:324+ / 531+). */
static const __code unsigned char *desc_ptr = 0;
static unsigned int desc_left = 0;

static void reply_zlp(void) { IEPBCTX0 = 0; }

/* Ship a 1- or 2-byte reply from a small immediate value. Used for the
 * short mandatory-request responses: GET_STATUS (2B, both 0),
 * GET_INTERFACE (1B alt=0), GET_CONFIGURATION (1B config=1). Clears
 * desc_left so any lingering multi-packet state from a prior transfer
 * doesn't get shipped after the IEP0-done handler thinks this is done. */
static void reply_short(unsigned char b0, unsigned char b1, unsigned char n)
{
    __xdata unsigned char *dst = (__xdata unsigned char *)EP0_IN_BUF_ADDR;
    dst[0] = b0;
    if (n > 1) dst[1] = b1;
    /* Release IN packet with byte-count `n`. TI UsbEng.c engEp0TxDone
     * writes IEPBCTX0 = length to arm the IN endpoint for the host's
     * next IN token. Rev 20 fcn.0x0B8C uses the same primitive. */
    IEPBCTX0 = n;
    desc_left = 0;
}
/* STALL is bit 3 of EP config regs; must OR the bit in, not mask it out.
 * Prior implementation used `&= 0xD7` which CLEARED bits 5 (TOGGLE) and
 * 3 (STALL) — the exact opposite of STALL. Reference: hwMacro.h:9-10
 * STALLInEp0/STALLOutEp0 both `|= 0x08`. */
static void stall(void)     { IEPCNF0 |= 0x08; OEPCNF0 |= 0x08; }

/* Ship the first 8-byte chunk of a descriptor and stash the rest for
 * the IEP0-done handler to continue. `len` is the true descriptor size;
 * we clamp to the host's wLength (SETPACK offset 0xFF2E-0xFF2F, LE). */
static void reply_desc(const __code unsigned char *src, unsigned int len)
{
    unsigned int want = ((unsigned int)XDATA(0xFF2F) << 8) | XDATA(0xFF2E);
    unsigned int total = (len > want) ? want : len;
    unsigned int chunk = (total > 8) ? 8 : total;
    unsigned int i;
    __xdata unsigned char *dst = (__xdata unsigned char *)EP0_IN_BUF_ADDR;
    for (i = 0; i < chunk; i++) dst[i] = src[i];
    /* Arm IN with the byte count of this chunk. Rev 20 fcn.0x0B8C
     * primitive; TI UsbEng.c engEp0TxDone. */
    IEPBCTX0 = (unsigned char)chunk;
    desc_ptr  = src + chunk;
    desc_left = total - chunk;
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
    /* Every new SETUP resets EP0 state: clear STALL from any prior
     * unsupported request, then set DATA1 toggle for the first response
     * packet. UsbEng.c:223-228 does exactly this order at the top of
     * engEp0SetupDone (STALLClrInEp0/OutEp0 then TOGGLEInEp0Data/OutEp0Data).
     *
     * Without the STALL-clear, once stall() has fired on any unsupported
     * request the STALL bit stays set and every future SETUP's response
     * also stalls → enumeration wedges. Reference: hwMacro.h:27-28
     * STALLClrIn/OutEp0 = `&= ~0x08`.
     *
     * Without the TOGGLE=1, every response ships DATA0 PID when host
     * expects DATA1 → host discards → retries → gives up (silent device).
     * Datasheet page 30 (Control Read Setup step 2); UsbEng.c:227-228.
     * These two together fixed the "flash succeeds, safety_net runs,
     * but no USB device appears" symptom on 2026-07-23. */
    IEPCNF0 &= ~0x08;
    OEPCNF0 &= ~0x08;
    IEPCNF0 |= 0x20;
    OEPCNF0 |= 0x20;

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
            case 0x00: /* GET_STATUS (USB 2.0 §9.4.5). Device / Interface
                        * / Endpoint variants all return 2 bytes. safety_net
                        * is bus-powered, no remote wakeup, no halted EPs,
                        * so both bytes are 0 for every recipient. Rev 20
                        * handles this via boot-ROM delegation (`ljmp
                        * 0x2F00`); safety_net does it inline. Fork
                        * disagreement 2026-07-24 — one audit claimed the
                        * STALL was survivable on macOS, another cited
                        * IOUSBFamily probing this after SET_ADDRESS.
                        * Handling it removes the ambiguity. */
                reply_short(0, 0, 2);
                return;
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
            case 0x08: /* GET_CONFIGURATION — macOS IOUSBFamily probes
                        * this after SET_CONFIGURATION. Return 1B: 1. */
                reply_short(1, 0, 1);
                return;
            case 0x09: /* SET_CONFIGURATION */
                reply_zlp();
                return;
            case 0x0A: /* GET_INTERFACE (USB 2.0 §9.4.4). Return 1B:
                        * current alt setting for the interface. safety_net
                        * has one interface (0) with only alt 0. Same
                        * fork-disagreement rationale as GET_STATUS above. */
                reply_short(0, 0, 1);
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
            /* Multi-packet descriptor continuation. If reply_desc had
             * leftover bytes to send, ship the next up-to-8 chunk. */
            if (desc_left) {
                unsigned int chunk = (desc_left > 8) ? 8 : desc_left;
                unsigned int i;
                __xdata unsigned char *dst =
                    (__xdata unsigned char *)EP0_IN_BUF_ADDR;
                for (i = 0; i < chunk; i++) dst[i] = desc_ptr[i];
                /* Rev 20 fcn.0x0B8C release-IN primitive; TI UsbEng.c
                 * engEp0TxDone continuation path. */
                IEPBCTX0 = (unsigned char)chunk;
                desc_ptr  += chunk;
                desc_left -= chunk;
            } else {
                /* Transfer complete. Rev 20's cleanup subroutine at
                 * 0x0B30 (fork audit 2026-07-23 corrected earlier
                 * mis-cited 0x0B1E): clears TOGGLE+STALL bits and
                 * zeroes both EP0 buffer counts, priming for the next
                 * SETUP. */
                IEPCNF0 &= 0xD7;   /* clear bit 5 TOGGLE + bit 3 STALL */
                OEPCNF0 &= 0xD7;
                IEPBCTX0 = 0;
            }
            OEPBCTX0 = 0;
            VECINT = 0;
            break;
        case VEC_OEP0:
            VECINT = 0;
            break;
        case VEC_RSTR:
            /* USB bus reset. Per TAS1020B datasheet §6.5.1.4, bus reset
             * CLEARS FEN. Init only set CONT (`USBCTL |= 0x80`) to
             * trigger host detection; the actual UBM-respond-to-USB
             * enable happens HERE. Without setting FEN in this handler
             * the UBM ignores every EP0 transaction after the first
             * reset — device stays silent on the bus.
             *
             * Also re-init EP0 config bytes: bus reset clears their
             * state. Match Rev 20's VEC_RSTR handler at 0x0F55-0x0F80
             * exactly: re-write EP0 CNFs, clear USBFADR, USBCTL |= 0xC0
             * (both CONT and FEN), USBIMSK = 0x9F (adds IEP0 mask bit
             * that init's 0xE5 doesn't have).
             *
             * This was the actual root cause of "safety_net runs but
             * doesn't enumerate" — pre-2026-07-23 fixes were all
             * downstream of a UBM that never woke up. */
            OEPCNF0 = 0x84;
            IEPCNF0 = 0x84;
            USBFADR = 0;
            USBCTL |= 0xC0;
            /* Rev 20 fcn.0x0F55 VEC_RSTR handler promotes USBIMSK to
             * 0x9F (adds IEP0 bit) after bus reset — cold init used
             * 0xE5. Match that pattern. */
            USBIMSK = 0x9F;

            pending_addr = 0xFF;
            desc_left = 0;
            VECINT = 0;
            break;
        case VEC_NONE:
            break;
        default:
            /* Any other unmasked interrupt source (SOF at 1 kHz, SUSR,
             * RESR, STPOW). Datasheet §6.5.7.3: "To clear any interrupt
             * and update the interrupt vector value to the next pending
             * interrupt, the MCU should simply write any value to this
             * register." Without this write the source stays latched
             * and the ISR re-fires on every return — device wedges
             * within ~1 ms of the first SOF because our USBIMSK=0xE5
             * unmasks SOF (bit 4). TI's usbIntrHandler (UsbEng.c:44-96)
             * writes VECINT = 0 in every case for exactly this reason. */
            VECINT = 0;
            break;
    }
}


/* --- Main ---------------------------------------------------------- */

void main(void)
{
    unsigned int settle;

    /* GATE ALL INTERRUPTS while we reconfigure. */
    EA = 0;

    /* DISCONNECT FIRST. Boot ROM leaves USBCTL with CONN asserted after
     * its DFU manifest; if we run init while the host is still trying to
     * enumerate boot ROM, the two enumerations race and the host times
     * out. Rev 20 (rev20_flat.asm 0x0AB8 and 0x08E2) explicitly writes
     * `USBCTL = 0` twice at the very top of main(). Because USBCTL is
     * boot-ROM-owned but this is an intentional disconnect (POLICY §2
     * carve-out), a direct write is correct here. */
    USBCTL = 0;

    /* Enable USB via GLOBCTL bit 0. Rev 20 rev20_flat.asm 0x100F does
     * `orl a, #0x01` on GLOBCTL, matching mboxfw/src/hw_init.c:54's
     * `GLOBCTL |= 0x01` ("enable USB last"). Boot ROM (RomBoot.c:33)
     * has already assigned GLOBCTL = 0x04 for 12MHz clock and LPWR; bit
     * 0 is the USB-enable bit set by app code. RMW because other bits
     * (2 = clock select, 6/7 = ROM modes) belong to the boot ROM.
     * (Earlier fork report claimed `|= 0x04` at TI Device.c:75 — that
     * citation was fabricated; no GLOBCTL write exists in TI
     * Application/*.c. Rev 20 asm is the authoritative source.) */
    GLOBCTL |= 0x01;

    /* MEMCFG SDW confirm — boot ROM already set it, be idempotent. */
    MEMCFG |= 0x01;

    /* Bring EP0 up. Values match TI engUsbInit (UsbEng.c:609-624):
     * IEPCNF0/OEPCNF0 = 0x84 (UBME | UBMIE, no STALL bit at init);
     * IEPBCTX0 = 0x80 (NAK IN); OEPBCTX0 = 0 (OUT ready to receive). */
    IEPBBAX0 = EP_BBAX(EP0_IN_BUF_ADDR);
    IEPBSIZ0 = EP_BSIZE(8);
    OEPBBAX0 = EP_BBAX(EP0_OUT_BUF_ADDR);
    OEPBSIZ0 = EP_BSIZE(8);
    /* Force `mov a,#0x80; movx @dptr,a`. SDCC with --opt-code-size
     * otherwise emits `rr a` here — rotating the a=0x01 left over from
     * the OEPBSIZ0 = 1 write above — to save one byte. Correct today,
     * silently broken the moment any nearby write changes A's value.
     * Volatile temp forces the immediate load. */
    /* IEPBCTX0 = 0x80 (NAK IN) — TI UsbEng.c engUsbInit line 620.
     * Rev 20 fcn.0x08F0 does the same. Volatile shim above forces
     * immediate load; see SDCC comment. */
    { volatile unsigned char nak_bit = 0x80; IEPBCTX0 = nak_bit; }
    OEPBCTX0 = 0;
    IEPCNF0  = 0x84;
    OEPCNF0  = 0x84;

    /* USBFADR = 0 — device starts unaddressed. TI UsbEng.c engUsbInit
     * line 635; Rev 20 rev20_flat.asm 0x0910. */
    USBFADR  = 0;

    /* USBIMSK = 0xE5. Bits: STPOW|STPRW|SETUP|SOF|SUSR|RESR|RSTR.
     * IEP0 (bit 3) is deliberately NOT unmasked — the USB engine
     * routes EP0 IN completion through the SETUP→TxDone chain
     * internally and we service continuation on the next SETUP or by
     * the engine re-firing. Sources agree on 0xE5:
     *   - TI reference UsbEng.c:640 engUsbInit final USBIMSK write
     *   - Rev 20 rev20_flat.asm 0x0917 master-init write
     *   - tools/rev20_diff_justifications.md row 326
     * A prior fix to 0x9F on 2026-07-23 was based on an unverified
     * theory; reverted. */
    USBIMSK = 0xE5;

    /* Level-triggered INT0. TI reference UsbEng.c:645 `IT0 = 0`. Edge-
     * triggered (the 8051 default) fires the ISR once on the pending
     * VECINT source, then relies on the source line dropping and re-
     * asserting to fire again. The USB engine drives INT0 by ORing all
     * unmasked USBIMSK sources; if any source stays asserted after
     * VECINT ack, the line never re-edges and the ISR never re-fires.
     * Level mode fires as long as any source is still pending — exactly
     * what an OR'd interrupt line needs. */
    IT0 = 0;

    /* Enable INT0 (USB engine) source. */
    EX0 = 1;

    /* Settle. Rev 20 (rev20_flat.asm 0x0AC5-0x0AD8) runs a ~65k-iter
     * outer loop between finishing init and enabling EA/CONN. This
     * gives the USB engine time to reach a stable idle state before
     * unmasking global interrupts and attaching D+. */
    for (settle = 0; settle < 0xFFFF; settle++) { }

    /* Unmask global interrupts. */
    EA = 1;

    /* Attach to bus. RMW `|= 0x80` — CONN only, FEN stays clear.
     * See USBCTL_ATTACH_BIT comment for full justification. */
    USBCTL |= USBCTL_ATTACH_BIT;

    /* Idle. All USB work is done in usb_isr() below. */
    for (;;) { }
}

/* USB interrupt service routine, installed at 8051 vector 0
 * (address 0x0003). Rev 20's ISR is at 0x0DAC (reached via
 * `LJMP 0x0DAC` at 0x0003); ours is at whatever address SDCC
 * places usb_isr, reached via SDCC-generated LJMP at 0x0003.
 *
 * The USB engine ORs all USB interrupt sources into EX0. When any
 * unmasked USBIMSK bit fires, this ISR runs, reads VECINT to
 * identify the source, dispatches, then clears VECINT (done inside
 * the per-case handlers in usb_service). */
void usb_isr(void) __interrupt(0)
{
    usb_service();
}

/* Defensive stubs for every other 8051/8052 interrupt vector.
 *
 * safety_net only enables EX0 (INT0/USB) via `EX0 = 1; EA = 1;`. Boot
 * ROM's UtilResetCPU (Utils.SRC:181) hands off with `IE = 0` so no
 * other enable bit is set. But SDCC doesn't emit a default handler
 * for vectors we don't declare — the bytes at 0x000B/0x0013/0x001B/
 * 0x0023/0x002B end up as optimizer-spilled code from unrelated
 * functions. If ANY future refactor (or a hardware glitch) sets an
 * ETx or ES or EX1 bit, that vector fires into garbage and CPU crashes.
 *
 * Declaring these as `__interrupt(N) { }` makes SDCC install a bare
 * RETI at each vector — 10 bytes total, one crash class eliminated.
 *
 * Vector numbers per SDCC mcs51 convention: 0=INT0, 1=Timer0, 2=INT1,
 * 3=Timer1, 4=UART, 5=Timer2. */
void isr_timer0(void) __interrupt(1) { }
void isr_int1  (void) __interrupt(2) { }
void isr_timer1(void) __interrupt(3) { }
void isr_uart  (void) __interrupt(4) { }
void isr_timer2(void) __interrupt(5) { }
