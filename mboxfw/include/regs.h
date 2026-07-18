#ifndef MBOXFW_REGS_H
#define MBOXFW_REGS_H
/*
 * TAS1020A UIFR register subset — only what our firmware actually touches.
 * Full map is in TI's Reg_stc1.h (reference/tas1020a/ti_uac_reference/ROM/).
 */

#include <mcs51/8051.h>   /* SFR declarations (P1, P3, TMOD, TCON, IE, IP, TH0, ...) */

/* SDCC's xdata addressing sugar. */
#define XDATA(addr)  (*(volatile __xdata unsigned char *)(addr))

/* USB endpoint 0 config */
#define IEPCNF0     XDATA(0xFF68)
#define IEPBBAX0    XDATA(0xFF69)
#define IEPBCTX0    XDATA(0xFF6B)
#define OEPCNF0     XDATA(0xFFA8)
#define OEPBBAX0    XDATA(0xFFA9)
#define OEPBCTX0    XDATA(0xFFAB)
#define OEPCNF0_HI  XDATA(0xFFB0)

/* USB endpoint 3 config (audio streaming) */
#define IEPCNF3     XDATA(0xFF60)
#define IEPBBAX3    XDATA(0xFF63)
#define IEPBSIZ3    XDATA(0xFF67)
#define OEPCNF3     XDATA(0xFF98)
#define OEPBBAX3    XDATA(0xFF9B)
#define OEPBSIZ3    XDATA(0xFF9F)

/* USB setup-packet block (SETPACK, 8 bytes at 0xFF28-0xFF2F) */
#define SETPACK_BMREQ  XDATA(0xFF28)
#define SETPACK_BREQ   XDATA(0xFF29)
#define SETPACK_WVAL_L XDATA(0xFF2A)
#define SETPACK_WVAL_H XDATA(0xFF2B)
#define SETPACK_WIDX_L XDATA(0xFF2C)
#define SETPACK_WIDX_H XDATA(0xFF2D)
#define SETPACK_WLEN_L XDATA(0xFF2E)
#define SETPACK_WLEN_H XDATA(0xFF2F)

/* Global control */
#define GLOBCTL     XDATA(0xFFB1)
#define GLOBCTL2    XDATA(0xFFFC)

/* Hardware I²C peripheral (used only for boot EEPROM at addr 0x50) */
#define I2CSTA      XDATA(0xFFC0)
#define I2CDAO      XDATA(0xFFC1)
#define I2CDAI      XDATA(0xFFC2)
#define I2CADR      XDATA(0xFFC3)

/* C-port (I²S master to codec and CS8427) */
#define CPTCTL      XDATA(0xFFD4)
#define CPTBRRX     XDATA(0xFFD5)
#define CPTBRTX     XDATA(0xFFD6)
#define CPTCNF1     XDATA(0xFFDC)
#define CPTCNF2     XDATA(0xFFDD)
#define CPTCNF3     XDATA(0xFFDE)
#define CPTCNF4     XDATA(0xFFDF)

/* DMA channels — channel 0 = playback, channel 2 = capture (per Rev 20) */
#define DMACTL0     XDATA(0xFFE0)
#define DMASRC0_L   XDATA(0xFFE5)
#define DMASRC0_M   XDATA(0xFFE6)
#define DMASRC0_H   XDATA(0xFFE7)
#define DMACTL1     XDATA(0xFFE1)
#define DMACTL2     XDATA(0xFFE2)
#define DMASRC2_L   XDATA(0xFFF7)
#define DMASRC2_M   XDATA(0xFFF8)
#define DMASRC2_H   XDATA(0xFFF9)

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

#endif /* MBOXFW_REGS_H */
