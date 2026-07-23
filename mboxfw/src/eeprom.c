/*
 * TAS1020A hardware-I²C EEPROM access — enter-DFU signature-invalidation
 * path only. See eeprom.h for the contract.
 *
 * The peripheral programming model is byte-for-byte ported from Rev 20's
 * boot-time EEPROM read (rev20_flat.asm 0x0C00 for the write helper,
 * 0x0CEF for the read helper). Rev 20 has been running against this
 * hardware for 20 years, so trusting the exact sequence — including the
 * pre-write "settle" delay after slave-address write, the bit-3
 * TX-done poll, and the ~5 ms post-write pause for the EEPROM's internal
 * program cycle — is safer than a first-principles rewrite.
 *
 * Bounded polls: every wait uses a 16-bit timeout so a hung peripheral
 * can't wedge the firmware. Failure is signalled to the caller, which
 * then falls back to the plain warm-reset path in usb.c.
 */

#include "regs.h"
#include "eeprom.h"

#define EEPROM_ADDR_WRITE  0xA0   /* 7-bit 0x50 << 1, R/W=0 */
#define EEPROM_ADDR_READ   0xA1   /* 7-bit 0x50 << 1, R/W=1 */

#define I2C_CTRL_STOP      0x01   /* bit 0: last byte, generate STOP */
#define I2C_CTRL_STARTRX   0x02   /* bit 1: start a read cycle */
#define I2C_CTRL_TXDONE    0x08   /* bit 3: TX byte accepted / write done */
#define I2C_CTRL_RXREADY   0x80   /* bit 7: RX byte available */

/* Post-slave-address settle. Rev 20 spins RAM until 0x0000 (two nested
 * 256-count djnz loops) = ~65k cycles = ~5 ms at 12 MHz. We reproduce
 * the same duration with an unsigned int decrement. */
static void i2c_settle(void)
{
    unsigned int i;
    for (i = 0; i < 0xFF00; i++) { }
}

/* Post-EEPROM-write hold. 24C64 needs up to 5 ms for the internal
 * program cycle to finish before the next START condition. Same
 * duration as Rev 20's tail-loop at 0x0C4B. */
static void eeprom_write_hold(void)
{
    unsigned int i;
    for (i = 0; i < 0xFF00; i++) { }
}

/* Poll bit until set. Returns 1 if seen within timeout, 0 otherwise.
 * Timeout ~ 65k reads which at ~10 cycles/read = ~500 us — plenty for
 * the EEPROM's ACK timing and short enough that a hung peripheral is
 * caught in under a millisecond. */
static unsigned char wait_bit(unsigned char mask)
{
    unsigned int t;
    for (t = 0; t < 0xFF00; t++) {
        if (I2C_CTRL & mask) return 1;
    }
    return 0;
}

unsigned char eeprom_write_byte(unsigned char addr_hi,
                                unsigned char addr_lo,
                                unsigned char data)
{
    /* Idle the peripheral (clear low 2 bits: start-tx and start-rx). */
    I2C_CTRL &= (unsigned char)0xFC;
    /* Slave address for the write cycle. */
    I2C_SADDR = EEPROM_ADDR_WRITE;
    i2c_settle();
    /* Word-address MSB. */
    I2C_TX = addr_hi;
    if (!wait_bit(I2C_CTRL_TXDONE)) return 0;
    /* Word-address LSB. */
    I2C_TX = addr_lo;
    if (!wait_bit(I2C_CTRL_TXDONE)) return 0;
    /* Data byte with STOP flag set — this is the final byte of the
     * transaction and must generate the I²C STOP condition. */
    I2C_CTRL |= I2C_CTRL_STOP;
    I2C_TX = data;
    if (!wait_bit(I2C_CTRL_TXDONE)) return 0;
    eeprom_write_hold();
    return 1;
}

unsigned char eeprom_read_byte(unsigned char addr_hi,
                               unsigned char addr_lo,
                               unsigned char *ok)
{
    /* Set up a dummy write to position the EEPROM's internal address
     * pointer, then re-address with R/W=1 to read the byte back. */
    I2C_CTRL &= (unsigned char)0xFC;
    I2C_SADDR = EEPROM_ADDR_WRITE;
    i2c_settle();
    I2C_TX = addr_hi;
    if (!wait_bit(I2C_CTRL_TXDONE)) { if (ok) *ok = 0; return 0xFF; }
    I2C_TX = addr_lo;
    if (!wait_bit(I2C_CTRL_TXDONE)) { if (ok) *ok = 0; return 0xFF; }
    /* Repeated START into the read cycle. */
    I2C_SADDR = EEPROM_ADDR_READ;
    I2C_CTRL |= I2C_CTRL_STARTRX;
    if (!wait_bit(I2C_CTRL_RXREADY)) { if (ok) *ok = 0; return 0xFF; }
    if (ok) *ok = 1;
    return I2C_RX;
}

unsigned char eeprom_smoke_test(void)
{
    static const unsigned char SCRATCH_HI = 0x1F;
    static const unsigned char SCRATCH_LO = 0xFF;   /* last byte of 8 KB */
    unsigned char ok;
    unsigned char v;

    /* Write 0xA5, read back — checks the write and read paths and the
     * TX/RX status bits. Pick a scratch address safely past our firmware
     * image (2 KB code + header + FF-fill leaves 0x1FFF unused). */
    if (!eeprom_write_byte(SCRATCH_HI, SCRATCH_LO, 0xA5)) return 0;
    v = eeprom_read_byte(SCRATCH_HI, SCRATCH_LO, &ok);
    if (!ok || v != 0xA5) return 0;

    /* Second round with 0x5A confirms it wasn't a coincidence
     * (e.g. bus stuck-at-0xA5). */
    if (!eeprom_write_byte(SCRATCH_HI, SCRATCH_LO, 0x5A)) return 0;
    v = eeprom_read_byte(SCRATCH_HI, SCRATCH_LO, &ok);
    if (!ok || v != 0x5A) return 0;

    /* Restore scratch to erased (0xFF) — leaves EEPROM tidy. */
    (void)eeprom_write_byte(SCRATCH_HI, SCRATCH_LO, 0xFF);
    return 1;
}

unsigned char eeprom_invalidate_signature(void)
{
    /* Header signature at offset 0x0002 = 0x12, offset 0x0003 = 0x34.
     * Zero both. Boot ROM checks these on the next power-on; mismatch
     * lands the device in bulletproof DFU (0xFFFF:0xFFFE). */
    if (!eeprom_write_byte(0x00, 0x02, 0x00)) return 0;
    if (!eeprom_write_byte(0x00, 0x03, 0x00)) return 0;
    return 1;
}
