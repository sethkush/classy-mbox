/*
 * CS8427 driver — bit-banged I²C on P1.3 (SCL) / P1.4 (SDA).
 *
 * Ports Rev 20's fcn.0x0C45 (single register write) and fcn.0x080B (boot
 * sequence of 10 register writes). Chip 7-bit address = 0x10, so the
 * 8-bit write byte is 0x20 (address<<1 | R/W=0).
 *
 * Rev 20 wraps every packet with a short spin on port bits (fcn.0x0E62
 * side-effects) and inserts a ~256-cycle djnz between register writes to
 * give the CS8427 time to internally settle. We reproduce that behavior
 * with explicit short delays.
 */

#include "regs.h"
#include "cs8427.h"

#define CS8427_ADDR_WRITE   0x20

static void bit_delay(void)
{
    /* Rev 20's inner-loop timing gives ~200 ns per SCL edge at 12 MHz.
     * With a stock TAS1020A clock the natural instruction time already
     * puts us in the CS8427's spec window; no explicit NOP needed. */
}

static void inter_reg_delay(void)
{
    /* Rev 20 djnz 0x2E from 0xFF gives ~256 machine cycles.
     * Count down to avoid the compare-at-uchar-max overflow warning.
     *
     * `i` MUST be volatile -- see the note in hw_init.c short_delay(). Without
     * it SDCC deletes all nine call sites in cs8427_boot_init() while leaving
     * this body in the image, so the whole register sequence ran with no
     * settling time between writes and the listing gave no hint of it. */
    volatile unsigned char i = 0xFF;
    do { } while (--i);
}

static void cs8427_start(void)
{
    /* Rev 20's start-condition emulation: assert start latch, then
     * kick the codec state-prep once (see fcn.0x0E62). We omit the
     * codec-side sync here — mode changes rewrite codec state anyway. */
    P1 |= (P1_CS8427_SCL_MASK | P1_CS8427_SDA_MASK);
    P1 &= ~P1_CS8427_SDA_MASK;    /* SDA falls while SCL high = START */
    P1 &= ~P1_CS8427_SCL_MASK;
}

static void cs8427_stop(void)
{
    P1 &= ~P1_CS8427_SDA_MASK;
    P1 |= P1_CS8427_SCL_MASK;
    P1 |= P1_CS8427_SDA_MASK;     /* SDA rises while SCL high = STOP */
}

static void cs8427_shift_byte(unsigned char b)
{
    unsigned char i;
    for (i = 0; i < 8; i++) {
        if (b & 0x80) {
            P1 |= P1_CS8427_SDA_MASK;
        } else {
            P1 &= ~P1_CS8427_SDA_MASK;
        }
        P1 |= P1_CS8427_SCL_MASK;
        bit_delay();
        P1 &= ~P1_CS8427_SCL_MASK;
        b <<= 1;
    }
    /* ACK bit: release SDA, pulse SCL, ignore actual ACK level. */
    P1 |= P1_CS8427_SDA_MASK;
    P1 |= P1_CS8427_SCL_MASK;
    P1 &= ~P1_CS8427_SCL_MASK;
}

void cs8427_write(unsigned char reg, unsigned char value)
{
    cs8427_start();
    cs8427_shift_byte(CS8427_ADDR_WRITE);
    cs8427_shift_byte(reg);
    cs8427_shift_byte(value);
    cs8427_stop();
}

/* Boot sequence — the ten register writes Rev 20 issues at 0x0855-0x08A2
 * (Rev 22 0x09FC-0x0A3x), in order. The register numbers and values are
 * byte-identical to stock; verify_cs8427.py pins them.
 *
 * The register NAMES below were wrong until 2026-07-30 — they were shifted by
 * one against the real map, so 0x01 was labelled "chip control 2", 0x02 "data
 * flow", 0x03 "clock source" and 0x11 "interrupt mask". Corrected against
 * reference/cs8427/alsa_cs8427.h. Wrong names in a driver are how the next
 * person reasons wrongly about it, which is most of what this file is for.
 *
 * WARNING: the FRAMING these writes go out in is still wrong — cs8427_write()
 * below speaks I²C, stock speaks 3-wire SPI with IRAM 0x25.7 as the chip
 * select. See FINDING_cs8427_is_spi_not_i2c.md and task #157. The values are
 * right; they are not currently reaching the part. */
void cs8427_boot_init(void)
{
    cs8427_write(0x04, 0x00);  inter_reg_delay();  /* CLOCKSOURCE  = 0, RUN clear */
    cs8427_write(0x13, 0x10);  inter_reg_delay();  /* UDATABUF */
    cs8427_write(0x04, 0x00);  inter_reg_delay();  /* CLOCKSOURCE  = 0 again */
    cs8427_write(0x04, 0x40);  inter_reg_delay();  /* CLOCKSOURCE  = RUN set */
    /* CONTROL1 = 0x01: SWCLK=0 so RMCK outputs the RECOVERED clock (this is
     * what clock mode 1 slaves the codec to), TCBLDIR=1 (TCBL an output),
     * INTMASK=0 so INT is active high — and note that reg 0x09 INT1MASK is
     * never written by stock or by us, so no interrupt source is ever
     * unmasked and INT can never assert. */
    cs8427_write(0x01, 0x01);  inter_reg_delay();  /* CONTROL1 */
    /* CONTROL2 = 0x20: HOLD = 01 = replace the sample with ZERO on a receiver
     * error; RMCKF=0 (256*Fsi); stereo receiver and transmitter. Unlock is
     * handled by muting in the data path, not by signalling a pin. */
    cs8427_write(0x02, 0x20);  inter_reg_delay();  /* CONTROL2 */
    cs8427_write(0x03, 0x0C);  inter_reg_delay();  /* DATAFLOW */
    /* SERIALINPUT / SERIALOUTPUT = 0x05: 24-bit, left-justified, SODEL=1 and
     * SOLRPOL=1 — the I²S convention. */
    cs8427_write(0x05, 0x05);  inter_reg_delay();  /* SERIALINPUT */
    cs8427_write(0x06, 0x05);  inter_reg_delay();  /* SERIALOUTPUT */
    cs8427_write(0x11, 0xFF);                      /* RECVERRMASK: all sources */
}
