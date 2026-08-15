#ifndef MBOXFW_REGS_H
#define MBOXFW_REGS_H
/*
 * TAS1020A UIFR register subset — only what our firmware actually touches.
 * Full map is in TI's Reg_stc1.h (reference/tas1020a/ti_uac_reference/ROM/).
 */

#include <mcs51/8051.h>   /* SFR declarations (P1, P3, TMOD, TCON, IE, IP, TH0, ...) */

/* SDCC's xdata addressing sugar. */
#define XDATA(addr)  (*(volatile __xdata unsigned char *)(addr))

/* Hardware I²C peripheral (0xFFC0-0xFFC3). Bit definitions verified
 * against TI's `reference/tas1020a/ti_uac_reference/ROM/I2c.h`:
 *   0xFFC0 = I2CSTA — control/status
 *     bit 0 (0x01) = STOP_WRITE (STOP flag for writes)
 *     bit 1 (0x02) = STOP_READ (STOP flag for reads)
 *     bit 3 (0x08) = XMIT_DATA_EMPTY (TX register empty / write done)
 *     bit 5 (0x20) = ERROR (NACK or bus fault — MUST be checked)
 *     bit 7 (0x80) = RCV_DATA_FULL (RX register full / read data ready)
 *     CLEAR_ALL = 0x54 — mask used to clear STOP/ERROR/done flags between
 *                        transactions while preserving cfg bits (freq/int)
 *   0xFFC1 = I2CDATO (TX data)
 *   0xFFC2 = I2CDATI (RX data)
 *   0xFFC3 = I2CADR (slave addr: 0xA0 write, 0xA1 read for EEPROM@0x50)
 *
 * Earlier drafts of this driver conflated bit 0 (STOP_WRITE) with
 * bit 1 (STOP_READ) and never checked ERROR, causing eeprom_read_byte
 * to hang and eeprom_smoke_test to silently fail (both software
 * recovery paths dead). Fixed 2026-07-22 after audit against TI I2c.c. */
#define I2C_STA     XDATA(0xFFC0)
#define I2C_TX      XDATA(0xFFC1)
#define I2C_RX      XDATA(0xFFC2)
#define I2C_SADDR   XDATA(0xFFC3)

/* TI I2c.h bit constants (bit 0..7 in I2CSTA). */
#define I2C_STOP_WRITE       0x01
#define I2C_STOP_READ        0x02
#define I2C_XMIT_DATA_EMPTY  0x08
#define I2C_ERROR            0x20
#define I2C_RCV_DATA_FULL    0x80
#define I2C_CLEAR_ALL        0x54

/* USB endpoint 0 config.
 *
 * Names are TI's, from reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h
 * lines 139/159/203/224. The +3 and +7 registers used to be called
 * IEPBCTX0/OEPBCTX0 here, which is not a TI name and not a datasheet name —
 * it was invented. The EP1/EP2 pass that corrected the same mistake for the
 * streaming endpoints (IEPDCNTX1 at 0xFF63, IEPDCNTY1 at 0xFF67) skipped the
 * EP0 pair, so one endpoint kept the invented spelling while its siblings
 * carried the real one. Grid: IEPCNFn = 0xFF68 - n*8, +3 = DCNTX, +7 = DCNTY;
 * OEPCNFn = 0xFFA8 - n*8, same offsets. */
#define IEPCNF0     XDATA(0xFF68)
#define IEPBBAX0    XDATA(0xFF69)
#define IEPBSIZ0    XDATA(0xFF6A)
#define IEPDCNTX0   XDATA(0xFF6B)
#define IEPDCNTY0   XDATA(0xFF6F)   /* Y buffer count — IEPCNFn + 7 */
#define OEPCNF0     XDATA(0xFFA8)
#define OEPBBAX0    XDATA(0xFFA9)
#define OEPBSIZ0    XDATA(0xFFAA)
#define OEPDCNTX0   XDATA(0xFFAB)
#define OEPDCNTY0   XDATA(0xFFAF)   /* Y buffer count — OEPCNFn + 7 */

/* EP0 packet buffers — placed manually in TAS1020B shared-mem window
 * starting at 0xF800. Stock writes OEPBBAX0=0x42 first, then IEPBBAX0 via
 * `inc a` (0x43):
 *   OEPBBAX0: Rev 20 fcn.0x0970 @ 0x0970, Rev 22 fcn.0x0891 @ 0x0891
 *   IEPBBAX0: Rev 20 fcn.0x0970 @ 0x0976, Rev 22 fcn.0x0891 @ 0x0897
 * #180: this cited "fcn.0x0982 disasm (rev20_flat.asm:1202-1220)". BOTH the
 * function label and the line range came from that listing, which
 * disassembles the EEPROM including its 18-byte header -- and 0x0982 - 0x12
 * = 0x0970, so even the function number was offset. flat-asm-ok.
 * Encoded as base_addr/8: 0x42*8 = 0xFA10 (OUT), 0x43*8 = 0xFA18 (IN).
 * (Earlier drafts had these swapped; symptomatic bug was that EP0 IN
 *  packets would land where the host wrote OUT, corrupting SETUP data.) */
#define EP0_OUT_BUF_ADDR   0xFA10
#define EP0_IN_BUF_ADDR    0xFA18
#define EP0_MAX_PACKET     8

/* Audio streaming buffers in TAS1020B shared memory. #162.
 *
 * NOW STOCK'S GEOMETRY EXACTLY: 640 B each, contiguous from 0xFA20.
 *
 *   Rev 20 fcn.0x0970: OEPBBAX2 = 0x44 @ 0x09A2, IEPBBAX1 = 0x94 @ 0x09A8,
 *                      OEPBSIZ2 = 0x50 @ 0x09AE, IEPBSIZ1 = 0x50 @ 0x09B4
 *   Rev 22 fcn.0x0891: the same four, @ 0x08C3 / 0x08C9 / 0x08CF / 0x08D5
 *
 * (IEPBSIZ1's constant is carried in A from the OEPBSIZ2 write immediately
 * before it — Rev 20 0x09AB / Rev 22 0x08CC hold
 * `90 ff 9a 74 50 f0 90 ff 62 f0` — which is why the access map
 * reports it as "write-computed" rather than as a literal 0x50. Same
 * accumulator-reuse pattern the DPTR-arithmetic warning in CLAUDE.md covers.)
 *
 * THE OLD 512 B RESTED ON A CONSTRAINT THAT DOES NOT EXIST. The previous
 * comment here justified 0x200 as fitting "below the 0xFF00 SFR boundary",
 * and then listed a free tail of 0xFF00-0xFF27 three lines later — the same
 * comment contradicting itself. Datasheet Figure 6-3 gives the real map:
 *
 *   0xFFB0-0xFFFF   memory-mapped registers        (80 B)
 *   0xFF30-0xFFAF   endpoint configuration blocks  (128 B)
 *   0xFF28-0xFF2F   setup data packet buffer       (8 B)
 *   0xFA64-0xFF27   ENDPOINT DATA BUFFERS          (1220 B)
 *   0xFA10-0xFA63   ROM support                    (84 B)
 *
 * Nothing is reserved at 0xFF00. The buffer region ends at 0xFF27, and
 * stock's capture buffer runs to exactly 0xFF1F.
 *
 * WHY THE SIZE MATTERS, per §6.4.4.4: for an ISOCHRONOUS endpoint, BSIZ
 * sizes a single circular buffer rather than one packet. At 48 kHz stereo
 * 24-bit a USB frame is 2ch x 3B x 48 = 288 B, so 512 B cannot hold two
 * frames and 640 B can. A buffer that cannot hold the frame being filled
 * plus the frame being drained has no slack for the host servicing late.
 *
 *   EP0 IN/OUT : 0xFA10-0xFA1F   (16 B, unchanged)
 *   EP2 OUT    : 0xFA20-0xFC9F   (640 B, playback)  base 0x44
 *   EP1 IN     : 0xFCA0-0xFF1F   (640 B, capture)   base 0x94
 *   free tail  : 0xFF20-0xFF27   (8 B)
 *
 * Both buffers sit partly in the region Figure 6-3 labels "ROM support".
 * That is deliberate and is what stock does: per TI Mmap.h line 24, the ROM
 * DFU code and the application never run at the same time, so the area is
 * app-owned while our firmware runs. mboxfw has ALREADY relied on this since
 * its first build — EP0's buffers at 0xFA10 are inside the same region, and
 * they demonstrably work on hardware. #162 extends an assumption that is
 * already load-bearing rather than making a new one. */
/* Back to STOCK's geometry, 2026-08-05, when 88.2/96 kHz were removed.
 *
 * These were 696 B playback / 576 B capture. Both values existed ONLY to serve
 * 96 kHz: 576 is a whole number of frames at 96 kHz (1) as well as 48 (2), and
 * 696 gave playback the slack it needs when a frame is 576 B rather than 288.
 * With the doubled rates gone the justification goes with them, and stock's
 * 640/640 -- proven across two shipped firmwares at 44.1 and 48 kHz -- is the
 * value to hold.
 *
 * This also restores the byte-identical write block #162 matched: stock carries
 * the constant in A across both BSIZ writes (Rev 20 0x09AB / Rev 22 0x08CC hold
 * `90 ff 9a 74 50 f0 90 ff 62 f0`), which only works when the two sizes are
 * equal. Two different constants forced a second `MOV A,#imm`.
 *
 * 640 is not a whole number of frames at 48 kHz (2.22) or of samples (106.67),
 * and that is fine here: it was only ever a problem at 96 kHz, where the slack
 * was 64 B instead of 352. Rev 22's SOF watchdog is the backstop stock ships
 * for it. See FINDING_46_no_bandwidth_above_24k.md for why the doubled rates
 * went. */
#define EP2_OUT_BUF_ADDR   0xFA20   /* playback buffer (host → device) */
#define EP1_IN_BUF_ADDR    0xFCA0   /* capture buffer  (device → host) */
#define EP_AUDIO_BUF_SIZE  0x0280   /* 640 B — stock's size, both endpoints */

/* USB audio streaming endpoints — Rev 20 uses EP1 IN + EP2 OUT.
 * (Per TI Reg_stc1.h: IEPCNF1=0xFF60, OEPCNF2=0xFF98. Earlier RE notes
 *  called these "EP3" — that was wrong; every occurrence of 0xFF60/0xFF98
 *  in the disasm is EP1-IN / EP2-OUT. Linux quirks wIndex=0x81 = EP1 IN
 *  confirms this.)
 */
/* Per TI Reg_stc1.h the streaming-EP register layout is:
 *   IEPCNFn = 0xFF68 - n*8      → IEPCNF1 = 0xFF60
 *   IEPBBAXn = IEPCNFn + 1
 *   IEPBSIZn = IEPCNFn + 2
 *   IEPBCTXn = IEPCNFn + 3
 *   OEPCNFn = 0xFFA8 - n*8      → OEPCNF2 = 0xFF98
 */
#define IEPCNF1     XDATA(0xFF60)   /* audio capture (device → host) */
#define IEPBBAX1    XDATA(0xFF61)
#define IEPBSIZ1    XDATA(0xFF62)
#define IEPDCNTX1    XDATA(0xFF63)
#define IEPDCNTY1    XDATA(0xFF67)   /* Y buffer count — IEPCNFn + 7 */

#define OEPCNF2     XDATA(0xFF98)   /* audio playback (host → device) */
#define OEPBBAX2    XDATA(0xFF99)
#define OEPBSIZ2    XDATA(0xFF9A)
#define OEPDCNTX2    XDATA(0xFF9B)
#define OEPDCNTY2    XDATA(0xFF9F)   /* Y buffer count — OEPCNFn + 7 */

/* #186 stage 2 — the playback FEEDBACK endpoint, EP2 IN.
 *
 * Same layout rule as the others: IEPCNFn = 0xFF68 - n*8, so n=2 gives 0xFF58.
 * The block was unused; capture holds EP1 IN and playback EP2 OUT.
 *
 * TI uses this same endpoint for this same purpose -- SoftPll.c writes INEP2_X
 * and arms IEPDCNTX2/IEPDCNTY2.
 *
 * BUFFER: the 8 free bytes at 0xFF20-0xFF27, the tail of the endpoint data
 * region (which ends at 0xFF27; capture runs to 0xFF1F). Eight is the BSIZ
 * granularity and therefore the minimum, and it is enough because an
 * ISOCHRONOUS endpoint gets ONE circular buffer -- the datasheet is explicit
 * that this differs from the X/Y pair a control/interrupt/bulk endpoint gets
 * in double-buffer mode (§6.4.4.4). So the 640/640 audio geometry, and the
 * byte-identical stock write block #162 matched, are untouched. */
#define IEPCNF2     XDATA(0xFF58)   /* playback feedback (device → host) */
#define IEPBBAX2    XDATA(0xFF59)
#define IEPBSIZ2    XDATA(0xFF5A)
#define IEPDCNTX2   XDATA(0xFF5B)
#define IEPDCNTY2   XDATA(0xFF5F)   /* Y buffer count — IEPCNFn + 7 */

#define EP_FEEDBACK_BUF_ADDR  0xFF20   /* 8 B, the free tail of the region */
#define EP_FEEDBACK_BUF_SIZE  8

/* #207 — the status interrupt endpoint, EP3 IN.
 *
 * WHY THIS FITS, since the first reading of the map said it did not.
 *
 * IEPBSIZx is the size of the PAIR of buffers, not of each one. The datasheet's
 * wording ("the size of the two data buffers") is ambiguous, but the feedback
 * endpoint settles it: 8 bytes at 0xFF20 with the endpoint-data-buffer region
 * ending at 0xFF27 only works if X and Y are 4 bytes each. Per-buffer would put
 * Y at 0xFF28, on top of the setup-packet buffer, and the device enumerates.
 *
 * And isochronous endpoints have no DBUF bit -- §6.4.4.6.2 gives bits 4:0 to the
 * BPS field -- so the audio endpoints always use both halves. 640 is therefore
 * 320 + 320, against a 294-byte maximum packet: 26 bytes of slack per half.
 *
 * So the region is full, but not TIGHT. Giving 8 bytes back from capture leaves
 * 316 per half, still 22 clear of the largest packet this firmware declares, and
 * more than that at 44.1 kHz where packets are 264/270. Those 8 bytes are this
 * endpoint's X and Y, 4 each, of which it uses 2. */
#define EP_AUDIO_CAPTURE_BUF_SIZE  0x0278   /* 632 = 316 + 316 (#207) */
#define EP_STATUS_BUF_ADDR    0xFF18   /* 8 B freed from the capture buffer */
#define EP_STATUS_BUF_SIZE    8

#define IEPCNF3     XDATA(0xFF50)   /* status interrupt (device → host) */
#define IEPBBAX3    XDATA(0xFF51)
#define IEPBSIZ3    XDATA(0xFF52)
#define IEPDCNTX3   XDATA(0xFF53)

/* USB setup-packet block (SETPACK, 8 bytes at 0xFF28-0xFF2F) */
#define SETPACK_BMREQ  XDATA(0xFF28)
#define SETPACK_BREQ   XDATA(0xFF29)
#define SETPACK_WVAL_L XDATA(0xFF2A)
#define SETPACK_WVAL_H XDATA(0xFF2B)
#define SETPACK_WIDX_L XDATA(0xFF2C)
#define SETPACK_WIDX_H XDATA(0xFF2D)
#define SETPACK_WLEN_L XDATA(0xFF2E)
#define SETPACK_WLEN_H XDATA(0xFF2F)

/* Global control + USB engine registers.
 * Per TI Reg_stc1.h:
 *   0xFFB0 = MEMCFG (memory config — SDW bit swaps ROM/RAM)
 *   0xFFB1 = GLOBCTL (global control — 24 MHz bit, SOF INT enable, ...)
 *   0xFFFC = USBCTL  (bit 7=CONN pull-up, bit 6=FEN, bit 0=SDW confirm)
 * Earlier drafts called 0xFFFC "GLOBCTL2" — that was wrong; it's USBCTL.
 * Earlier drafts called 0xFFB0 "OEPCNF0_HI" — also wrong; it's MEMCFG.
 * Both names carry values that happen to match Rev 20's boot sequence,
 * so behavior is unchanged, but the names are corrected for future
 * clarity. */
#define MEMCFG      XDATA(0xFFB0)
#define GLOBCTL     XDATA(0xFFB1)
#define VECINT      XDATA(0xFFB2)   /* vectored interrupt source */
#define IEPINT      XDATA(0xFFB3)
#define OEPINT      XDATA(0xFFB4)
#define USBCTL      XDATA(0xFFFC)   /* CONN + FEN + SDW */
#define USBIMSK     XDATA(0xFFFD)
#define USBSTA      XDATA(0xFFFE)
#define USBFADR     XDATA(0xFFFF)

/* USBCTL bits (TI Reg_stc1.h). CONN + FEN = 0xC0 is what engUsbInit
 * writes at the end to attach to the bus. */
#define USBCTL_CONN 0x80
#define USBCTL_FEN  0x40
#define USBCTL_SDW  0x01

/* VECINT interrupt-source codes (TI Reg_stc1.h) */
#define VEC_OEP0    0x00
#define VEC_IEP0    0x08
#define VEC_IEP1    0x09
#define VEC_OEP2    0x02
#define VEC_SETUP   0x12
#define VEC_SOF     0x14
#define VEC_RESR    0x15
#define VEC_SUSR    0x16
#define VEC_RSTR    0x17
#define VEC_NONE    0x24

/* EP buffer base for IEPBBAXn/OEPBBAXn is (addr - 0xF800) >> 3. */
#define STC_BUFFER_BASE  0xF800
#define EP_BBAX(addr)    (unsigned char)(((addr) - STC_BUFFER_BASE) >> 3)
#define EP_BSIZE(bytes)  (unsigned char)((bytes) >> 3)

/* (I²C peripheral aliases defined above with correct bit map) */

/* C-port (I²S master to codec and CS8427) */
/* Codec-port registers, names per TI Reg_stc1.h.
 *
 * These were previously named CPTCTL / CPTBRRX / CPTBRTX / CPTCNF1-4 and
 * were WRONG — the labels did not correspond to TI's at any of these
 * addresses, and CPTCNF1-4 in particular were reversed. The addresses and
 * values were right (transcribed correctly from Rev 20 by address), so the
 * writes landed where they should; only the names lied. Renamed to match
 * TI so the next person comparing against a trace is not misled. */
#define CPTRXCNF4   XDATA(0xFFD4)
#define CPTRXCNF3   XDATA(0xFFD5)
#define CPTRXCNF2   XDATA(0xFFD6)
/* 0xFFDC. #164, and the answer is not the one the task assumed.
 *
 * TI's Reg_stc1.h defines BOTH names at this one address (lines 49-50), and
 * the datasheet settles which is canonical and why there are two: §6.5.4.5
 * "Codec Port Interface Control AND STATUS Register (CPTCTL - Address FFDCh)".
 * The register-map table at §6.5.4 lists it as CPTCTL. One address, mixed
 * types, per the datasheet's own bit table:
 *
 *   bit 7  RXF   R    receive data register full   (hardware sets)
 *   bit 6  RXIE  R/W  receive interrupt enable
 *   bit 5  TXE   R    transmit data register empty (hardware sets)
 *   bit 4  TXIE  R/W  transmit interrupt enable
 *   bit 3   —    R    reserved
 *   bit 2:1 CID  R/W  codec ID (AC'97 primary/secondary select)
 *   bit 0  CRST  R/W  codec reset -> the CRESET output pin
 *
 * CPTCTL, because a register you WRITE is named for what the write does, and
 * every bit mboxfw writes here is an R/W control bit. CPTSTA is TI's alias for
 * reading the same address.
 *
 * THIS CLOSES THE 0x70 QUESTION. hw_init writes 0x50 and telemetry reads 0x70
 * back, and that gap was flagged as unexplained in four FINDING docs (147,
 * 170, capture_works_anyway, globctl_bits_named_and_cpten_missing), each time
 * as "hardware is not holding the value written". Decode it instead:
 *
 *   write 0x50 = RXIE | TXIE            -- both R/W, both take
 *   read  0x70 = RXIE | TXE | TXIE      -- the same two, plus TXE
 *
 * TXE is bit 5, read-only, and set by hardware when the transmit data register
 * has been sent to the codec. Every writable bit reads back exactly as
 * written; the one extra bit is hardware reporting its own state, which is the
 * entire purpose of a control-and-status register. Nothing was ever wrong. The
 * name was: "STA" said the whole register was status, hw_init treated the
 * whole thing as control, and the truth is that it is per-bit.
 *
 * The datasheet also refutes the "may be clear-on-read" caution this name
 * invented in telemetry.c: RXF is cleared by reading the receive DATA
 * register and TXE by writing the transmit data register -- not by reading
 * this one. Reading CPTCTL for telemetry is free of side effects.
 *
 * See FINDING_cptctl_is_control_and_status.md. */
#define CPTCTL      XDATA(0xFFDC)
#define CPTCNF4     XDATA(0xFFDD)
#define CPTCNF3     XDATA(0xFFDE)
#define CPTCNF2     XDATA(0xFFDF)
#define CPTCNF1     XDATA(0xFFE0)

/* Adaptive clock generator 1. These are NOT DMA registers.
 *
 * 0xFFE1 was named DMACTL1 here and 0xFFE5-7 were named DMASRC0_L/M/H.
 * All four names were invented; TI Reg_stc1.h calls them ACGCTL and
 * ACG1FRQ2/1/0, and the datasheet §6.5.3.11 / §6.5.3.1-3 agrees. This is
 * the same class of error already documented below for 0xFFE2, and it had
 * the same consequence: streaming.c wrote `DMACTL1 |= 0xC0` believing it
 * armed the two DMA channels, when it was setting clock-generator enable
 * bits. The real DMA channels were never enabled, so the capture endpoint
 * buffer was never filled and every isochronous IN packet came back
 * zero-length (measured with usbmon 2026-07-28 against a stock Rev 18
 * unit delivering 288 B/frame).
 *
 * The 24-bit values written to ACG1FRQ/ACG2FRQ (0x204B6A, 0x0FA861) are
 * clock-generator frequency words, not DMA source addresses. */
#define ACGCTL      XDATA(0xFFE1)   /* TI Reg_stc1.h; datasheet §6.5.3.11 */

/* ACG MCLK capture register — the hardware that makes #186 possible.
 *
 * A 16-bit FREE-RUNNING counter clocked at the MCLKO frequency. At every USB
 * SOF (or PSOF) the counter value is latched into this register and stays
 * valid for the whole frame (datasheet §2.2.6, "Adaptive Clock Generator MCLK
 * Capture Register", ACGCAPH at FFE3h / ACGCAPL at FFE4h; TI Reg_stc1.h lines
 * 63-64 names them identically).
 *
 * Differencing successive captures gives MCLK cycles per USB frame — the
 * device's own clock measured against the host's frame clock, on-chip, with no
 * timer arithmetic. That is the error signal a feedback endpoint reports, and
 * it is exactly what TI's SoftPll.c reads.
 *
 * NOT USED BY STOCK. Neither Rev 20 nor Rev 22 contains a DPTR load of 0xFFE3
 * or 0xFFE4 — Rev 22 ported the tail of TI's softPll() (the DMA realignment
 * that became fcn.0x0D58) and dropped the measurement half. See
 * FINDING_186_ti_softpll_is_the_feedback_endpoint.md.
 *
 * Read HIGH then LOW is NOT required here the way it is for DMABCNT0: the
 * latch is frame-stable by construction, so the pair cannot tear within a
 * frame. Order is chosen to match TI's (LOW then HIGH, SoftPll.c). */
#define ACGCAPH     XDATA(0xFFE3)   /* TI Reg_stc1.h:63; datasheet §2.2.6 */
#define ACGCAPL     XDATA(0xFFE4)   /* TI Reg_stc1.h:64; datasheet §2.2.6 */
#define ACG1FRQ2    XDATA(0xFFE5)   /* TI Reg_stc1.h; datasheet §6.5.3.3 */
#define ACG1FRQ1    XDATA(0xFFE6)   /* TI Reg_stc1.h; datasheet §6.5.3.2 */
#define ACG1FRQ0    XDATA(0xFFE7)   /* TI Reg_stc1.h; datasheet §6.5.3.1 */

/* The real DMA channel registers (datasheet §6.5.2, Table "MCU memory-
 * mapped registers" at FFE8h-FFF4h).
 *
 * DMACTL bits (§6.5.2.3): 7=DMAEN 6=HSKEN 5:4=rsvd 3=EPDIR 2:0=EPNUM.
 *   Rev 20 boot writes DMACTL0 = 0x02 → EPDIR=0 (OUT), EPNUM=2 → EP2 OUT,
 *   i.e. channel 0 is PLAYBACK; and DMACTL1 = 0x09 → EPDIR=1 (IN),
 *   EPNUM=1 → EP1 IN, i.e. channel 1 is CAPTURE. Neither has DMAEN set at
 *   boot; Rev 20 sets bit 7 at SET_INTERFACE time and clears it on stop.
 * DMATSH bits (§6.5.2.2): 7:6=BPTS (bytes per time slot, 10b = 3 bytes),
 *   5:0=TSL(13:8). DMATSL (§6.5.2.1) = TSL(7:0).
 *   Rev 20 writes DMATSH=0x80, DMATSL=0x03 on both channels: 3 bytes per
 *   slot on time slots 0 and 1 = 6 bytes per audio sample = stereo 24-bit,
 *   which matches IEPCNF1 = 0xC5 (ISO, BPS field = 5 → 6 bytes/sample) and
 *   the 288 B/frame stock delivers at 48 kHz. */
#define DMACTL0     XDATA(0xFFE8)   /* playback  — EP2 OUT */
#define DMATSH0     XDATA(0xFFE9)
#define DMATSL0     XDATA(0xFFEA)
#define DMABCNT0L   XDATA(0xFFEB)   /* read-only, updated every SOF */
#define DMABCNT0H   XDATA(0xFFEC)
#define DMACTL1     XDATA(0xFFEE)   /* capture — EP1 IN */
#define DMATSH1     XDATA(0xFFEF)
#define DMATSL1     XDATA(0xFFF0)
#define DMABCNT1L   XDATA(0xFFF3)
#define DMABCNT1H   XDATA(0xFFF4)

#define DMA_EN      0x80            /* DMACTLn bit 7 = DMAEN */

/* 0xFFE2 and 0xFFF6-0xFFF9 are NOT DMA registers. TI Reg_stc1.h names
 * them as the adaptive clock generators. Earlier revisions of this file
 * called 0xFFE2 "DMACTL2" and 0xFFF7-0xFFF9 "DMASRC2_L/M/H"; those names
 * were invented here and appear nowhere in TI's header. The bad names
 * produced a real bug in streaming_set_rate (writing 0x00 to what it
 * thought was a DMA halt register, when Rev 20 writes 0x10 to a clock
 * control register) — see the comment there.
 *
 * The mistake is easy to repeat: Reg_stc1.h contains COMMENTED-OUT
 * defines that put DMA names on these same addresses. Any tooling that
 * greps the header for `#define ... stc_sfr(0x....)` without excluding
 * comment lines will pick up the dead aliases. Five addresses are
 * affected: 0xFFF4, 0xFFF6, 0xFFF7, 0xFFF8, 0xFFF9. */
#define ACGDCTL     XDATA(0xFFE2)   /* TI Reg_stc1.h */
#define ACG2DCTL    XDATA(0xFFF6)   /* TI Reg_stc1.h */
#define ACG2FRQ2    XDATA(0xFFF7)   /* TI Reg_stc1.h */
#define ACG2FRQ1    XDATA(0xFFF8)   /* TI Reg_stc1.h */
#define ACG2FRQ0    XDATA(0xFFF9)   /* TI Reg_stc1.h */

/* The DMASRC0_x and DMASRC2_x aliases that used to live here are gone.
 * They named clock-generator frequency bytes as DMA source pointers and
 * were the proximate cause of the zero-length-isoc bug; see the ACGCTL
 * block above. Use ACG1FRQn / ACG2FRQn. */

/* Bit-bang GPIO helpers on P1 — see NOTES.md for pin map.
 * P1.0/1/2 = codec, P1.3/4 = CS8427 I²C, P1.5/6/7 = input mux shift-reg.
 */
#define P1_CODEC_SDIN_MASK   0x01   /* P1.0 */
#define P1_CODEC_LATCH_MASK  0x02   /* P1.1 */
#define P1_CODEC_SCLK_MASK   0x04   /* P1.2 */
#define P1_CS8427_SCL_MASK   0x08   /* P1.3 */
#define P1_CS8427_SDA_MASK   0x10   /* P1.4 */
#define P1_MUX_SCLK_MASK     0x20   /* P1.5 */
#define P1_MUX_LATCH_MASK    0x40   /* P1.6 */
#define P1_MUX_DATA_MASK     0x80   /* P1.7 */

/* Front-panel buttons on P3 — ACTIVE HIGH.
 *
 * The board holds these three pins LOW at rest and a press drives them HIGH,
 * so the internal P3 pull-ups must be DISABLED (GLOBCTL bit 1, P3PUDIS) or
 * they override the board and every pin reads a stuck 1. Stock sets P3PUDIS
 * in hw_master_init for exactly this reason.
 *
 * This comment read "active-low with pull-ups" from the first port until
 * 2026-08-03. It was wrong, it is why P3PUDIS looked optional, and it is why
 * the boot-time DFU escape was written with an inverted test. Proof of the
 * polarity, from the stock image rather than from this line: see
 * firmware_stock/decomp/FINDING_buttons_are_active_high.md. */
#define P3_BTN_CH1_MASK   0x08   /* P3.3 = channel-1 source cycle button */
#define P3_BTN_CH2_MASK   0x10   /* P3.4 = channel-2 source cycle button */
/* P3.5 toggles MONO, not 48V phantom power. Rev 20 fcn.0x0ED5 @ 0x0EE7 calls
 * fcn.0x1028, whose entire body toggles bit 0x1E = RAM[0x23].6; Rev 22
 * fcn.0x0F31 @ 0x0F41 calls fcn.0x1020, identical. 48V is a mechanical
 * latching switch and has no firmware bit — see mux.h. */
#define P3_BTN_MONO_MASK  0x20   /* P3.5 = mono fold-down toggle */

/*
 * RESET_TO_BOOT_ROM — enter the boot ROM immediately, not on next power
 * cycle. Byte-for-byte match to TI Utils.SRC UtilResetBootCPU (lines
 * 119-160): mask INT0, USBCTL SDW-confirm ON, flip MEMCFG.SDW off,
 * USBCTL SDW-confirm OFF, ljmp 0x8000.
 *
 * The `clr ea` is load-bearing: without it, any USB interrupt firing
 * between the SDW-clearing `movx @dptr,a` and the `ljmp` vectors to
 * 0x0003 with the memory map already flipped — CPU sees boot ROM
 * bytes at 0x0003 instead of our ISR and jumps into undefined code.
 *
 * A plain `ljmp 0` with SDW=1 restarts app code (we jump back into our
 * own reset vector in RAM). Signature-invalidation would then take
 * effect only on next physical power cycle — turning any "trigger DFU"
 * path into a "please unplug" instruction. Fork audit 2026-07-24.
 *
 * Never returns.
 *
 * !!! BROKEN — DO NOT USE. Diagnosed 2026-07-27. !!!
 *
 * This macro cannot work inlined into our own code. TI Utils.SRC states
 * twice, at lines 112 and 172, that UtilResetBootCPU must be
 * "compile w #pragma SRC and link w code segment 8003h" — i.e. the
 * routine executes from INSIDE THE BOOT ROM at 0x8003.
 *
 * That is load-bearing. Clearing MEMCFG.SDW swaps the shadow ROM in over
 * 0x0000.., so any code performing the flip from RAM unmaps ITSELF: the
 * instruction fetch after `movx @dptr,a` comes from boot-ROM bytes at the
 * current PC instead of our code. TI's copy is immune only because it
 * sits at 0x8xxx, which the swap does not touch.
 *
 * Observed: every caller of this macro hangs the CPU dead — safety_net's
 * DFU trigger (2026-07-27) and mboxfw's before it. Both were previously
 * blamed on ISR context; that was wrong. Moving the call to the main loop
 * does NOT help, because the defect is the execution address, not the
 * interrupt state.
 *
 * A correct version has to hand control to the boot ROM BEFORE flipping
 * SDW — but the entry address of UtilResetBootCPU on real Mbox silicon is
 * unverified (the boot ROM has never been dumped; our TI reference is a
 * third-party GitHub copy). Do not guess an address here. */
#define RESET_TO_BOOT_ROM() do { \
    __asm__("clr  ea");                                     \
    __asm__("mov  dptr,#0xFFFC");                           \
    __asm__("movx a,@dptr");                                \
    __asm__("orl  a,#0x01");                                \
    __asm__("movx @dptr,a");                                \
    __asm__("mov  dptr,#0xFFB0");                           \
    __asm__("movx a,@dptr");                                \
    __asm__("anl  a,#0xFE");                                \
    __asm__("movx @dptr,a");                                \
    __asm__("mov  dptr,#0xFFFC");                           \
    __asm__("movx a,@dptr");                                \
    __asm__("anl  a,#0xFE");                                \
    __asm__("movx @dptr,a");                                \
    __asm__("ljmp 0x8000");                                 \
} while (0)

#endif /* MBOXFW_REGS_H */
