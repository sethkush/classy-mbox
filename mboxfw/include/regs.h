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

/* USB endpoint 0 config */
#define IEPCNF0     XDATA(0xFF68)
#define IEPBBAX0    XDATA(0xFF69)
#define IEPBSIZ0    XDATA(0xFF6A)
#define IEPBCTX0    XDATA(0xFF6B)
#define OEPCNF0     XDATA(0xFFA8)
#define OEPBBAX0    XDATA(0xFFA9)
#define OEPBSIZ0    XDATA(0xFFAA)
#define OEPBCTX0    XDATA(0xFFAB)

/* EP0 packet buffers — placed manually in TAS1020A shared-mem window
 * starting at 0xF800. Rev 20's fcn.0x0982 disasm (rev20_flat.asm:1202-1220)
 * shows OEPBBAX0=0x42 written first, then IEPBBAX0 via `inc a` (0x43).
 * Encoded as base_addr/8: 0x42*8 = 0xFA10 (OUT), 0x43*8 = 0xFA18 (IN).
 * (Earlier drafts had these swapped; symptomatic bug was that EP0 IN
 *  packets would land where the host wrote OUT, corrupting SETUP data.) */
#define EP0_OUT_BUF_ADDR   0xFA10
#define EP0_IN_BUF_ADDR    0xFA18
#define EP0_MAX_PACKET     8

/* Audio streaming buffers — larger, further into the shared window. */
/* Audio streaming buffers in TAS1020A shared memory.
 *
 * Sizing constraint (48 kHz stereo 24-bit): 2ch × 3B × 48 samples/frame
 * = 288 B/frame. A buffer smaller than one frame silently truncates
 * every USB packet — the previous 256 B (0x100) size did exactly that
 * for 48 kHz and would have shipped garbled audio despite otherwise-
 * correct enumeration (found by tools/diff_vs_rev20.py bulk resolution
 * 2026-07-23). Rev 20 uses 640 B (0x280) — 2.2× a 48 kHz frame, room
 * for jitter + high-water headroom.
 *
 * We use 512 B (0x200) to fit both buffers below the 0xFF00 SFR
 * boundary while keeping ≥ 1.7× 48 kHz frame headroom:
 *   EP0 IN/OUT   : 0xFA10-0xFA1F (16 B, unchanged)
 *   EP1 IN       : 0xFB00-0xFCFF (512 B, capture)
 *   EP2 OUT      : 0xFD00-0xFEFF (512 B, playback)
 *   free tail    : 0xFF00-0xFF27 (SFRs start at 0xFF28)
 * All non-overlapping, all below the SFR window.
 *
 * If we ever add 88.2/96 kHz support, revisit: 96 kHz needs 576 B/frame
 * → 512 B truncates. Bump both to 0x300 and move EP2 to 0xFE00 (then
 * EP1 free-tail collision at 0xFE00 forces reworking EP1 too). */
#define EP1_IN_BUF_ADDR    0xFB00   /* capture buffer  (device → host) */
#define EP2_OUT_BUF_ADDR   0xFD00   /* playback buffer (host → device) */
#define EP_AUDIO_BUF_SIZE  0x0200   /* 512 B — ≥ 288 B (48 kHz frame) + slack */

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
#define IEPBCTX1    XDATA(0xFF63)

#define OEPCNF2     XDATA(0xFF98)   /* audio playback (host → device) */
#define OEPBBAX2    XDATA(0xFF99)
#define OEPBSIZ2    XDATA(0xFF9A)
#define OEPBCTX2    XDATA(0xFF9B)

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
#define CPTSTA      XDATA(0xFFDC)
#define CPTCNF4     XDATA(0xFFDD)
#define CPTCNF3     XDATA(0xFFDE)
#define CPTCNF2     XDATA(0xFFDF)
#define CPTCNF1     XDATA(0xFFE0)

/* DMA channels — channel 0 = playback, channel 2 = capture (per Rev 20) */
#define DMASRC0_L   XDATA(0xFFE5)
#define DMASRC0_M   XDATA(0xFFE6)
#define DMASRC0_H   XDATA(0xFFE7)
#define DMACTL1     XDATA(0xFFE1)

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

/* Compatibility aliases for the three ACG2FRQ bytes, kept because the
 * streaming code writes them as a 24-bit frequency word (0x204B6A) and
 * reads better under the old names in that context. Same addresses. */
#define DMASRC2_L   ACG2FRQ2
#define DMASRC2_M   ACG2FRQ1
#define DMASRC2_H   ACG2FRQ0

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

/* Front-panel buttons on P3 — active-low with pull-ups. */
#define P3_BTN_CH1_MASK   0x08   /* P3.3 = channel-1 source cycle button */
#define P3_BTN_CH2_MASK   0x10   /* P3.4 = channel-2 source cycle button */
#define P3_BTN_48V_MASK   0x20   /* P3.5 = phantom power toggle */

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
 * Never returns. */
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
