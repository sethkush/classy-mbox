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

/* Boot sequence — literal port of Rev 20 fcn.0x080B.
 * See NOTES.md § "CS8427 boot sequence" for register meanings. */
void cs8427_boot_init(void)
{
    cs8427_write(0x04, 0x00);  inter_reg_delay();  /* Clock ctrl: reset */
    cs8427_write(0x13, 0x10);  inter_reg_delay();  /* Channel status format */
    cs8427_write(0x04, 0x00);  inter_reg_delay();  /* Clock ctrl: still reset */
    cs8427_write(0x04, 0x40);  inter_reg_delay();  /* Clock ctrl: RUN=1 */
    cs8427_write(0x01, 0x01);  inter_reg_delay();  /* Chip control 2 */
    cs8427_write(0x02, 0x20);  inter_reg_delay();  /* Data flow control */
    cs8427_write(0x03, 0x0C);  inter_reg_delay();  /* Clock source ctrl 3 */
    cs8427_write(0x05, 0x05);  inter_reg_delay();  /* Serial audio input fmt */
    cs8427_write(0x06, 0x05);  inter_reg_delay();  /* Serial audio output fmt */
    cs8427_write(0x11, 0xFF);                       /* Interrupt mask: enable */
}
