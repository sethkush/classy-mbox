/*
 * TAS1020A hardware-I²C EEPROM driver — enter-DFU signature-invalidation
 * path only. Ported from TI's reference driver at
 * `reference/tas1020a/ti_uac_reference/ROM/I2c.c::I2CAccess`, adjusted
 * for our fixed-purpose use (single-byte read/write against the 24C64
 * EEPROM at slave address 0x50, always word-address mode).
 *
 * The previous version was written from Rev 20 disassembly guesses and
 * had two bugs that made both software recovery paths silently fail
 * (found by audit 2026-07-22):
 *   - eeprom_read_byte forgot the DUMMY `I2CDATO = 0xFF` write that
 *     actually fires the read cycle after the read-address load.
 *     Without it, the peripheral never transfers, wait for RCV_DATA_FULL
 *     times out, invalidation is skipped, warm reset lands right back
 *     in the same broken firmware.
 *   - wait_bit never checked the ERROR bit and CLEAR_ALL between
 *     transactions left stale done/error flags. TI's I2c.h defines
 *     ERROR=0x20 (NACK/bus fault) and CLEAR_ALL=0x54 (mask that clears
 *     STOP/ERROR/done flags while preserving freq/interrupt cfg).
 *
 * All bit constants and the read/write sequence match TI's I2c.c
 * verbatim now. Register aliases (I2C_STA / I2C_TX / I2C_RX / I2C_SADDR)
 * are in regs.h.
 */

#include "regs.h"
#include "eeprom.h"

#define EEPROM_ADDR_WRITE  0xA0   /* 7-bit 0x50 << 1, R/W=0 */
#define EEPROM_ADDR_READ   0xA1   /* 7-bit 0x50 << 1, R/W=1 */

/* Post-EEPROM-write hold. 24C64 needs up to 5 ms for the internal
 * program cycle to finish before the next START condition. */
static void eeprom_write_hold(void)
{
    unsigned int i;
    for (i = 0; i < 0xFF00; i++) { }
}

/* Wait for `mask` bit to appear in I2C_STA, OR abort early on ERROR.
 * Returns 1 on success, 0 on ERROR or timeout. Modeled on TI's
 * WaitOnI2C — same "either the wanted bit OR ERROR" exit condition. */
static unsigned char wait_bit(unsigned char mask)
{
    unsigned int t;
    unsigned char s;
    for (t = 0; t < 0xFF00; t++) {
        s = I2C_STA;
        if (s & I2C_ERROR)  return 0;
        if (s & mask)       return 1;
    }
    return 0;
}

/* Write one byte to EEPROM offset (addr_hi << 8 | addr_lo). Returns
 * 1 on success, 0 on any failure. Sequence matches TI's I2CAccess
 * for I2C_START | I2C_WRITE | I2C_STOP | I2C_WORD_ADDR_TYPE with nLen=1. */
unsigned char eeprom_write_byte(unsigned char addr_hi,
                                unsigned char addr_lo,
                                unsigned char data)
{
    I2C_STA   &= I2C_CLEAR_ALL;
    I2C_SADDR  = EEPROM_ADDR_WRITE;

    /* Word-address MSB */
    I2C_TX = addr_hi;
    if (!wait_bit(I2C_XMIT_DATA_EMPTY)) return 0;
    /* Word-address LSB */
    I2C_TX = addr_lo;
    if (!wait_bit(I2C_XMIT_DATA_EMPTY)) return 0;

    /* This is the LAST data byte — set STOP_WRITE before pushing it so
     * the peripheral generates the I²C STOP condition after transmit. */
    I2C_STA |= I2C_STOP_WRITE;
    I2C_TX = data;
    if (!wait_bit(I2C_XMIT_DATA_EMPTY)) return 0;

    I2C_STA &= I2C_CLEAR_ALL;
    eeprom_write_hold();
    return 1;
}

/* Read one byte from EEPROM offset (addr_hi << 8 | addr_lo). Returns
 * the byte on success (with *ok=1), 0xFF on failure (with *ok=0).
 * Sequence: word-address write phase to position the internal pointer,
 * then repeated-START into read phase with DUMMY 0xFF trigger + STOP_READ.
 * Matches TI's I2CAccess for I2C_START | I2C_READ | I2C_STOP | WORD_ADDR
 * with nLen=1 (the `nLen == 1` fast path at I2c.c:99). */
unsigned char eeprom_read_byte(unsigned char addr_hi,
                               unsigned char addr_lo,
                               unsigned char *ok)
{
    /* --- write phase: position the internal address pointer --- */
    I2C_STA   &= I2C_CLEAR_ALL;
    I2C_SADDR  = EEPROM_ADDR_WRITE;
    I2C_TX = addr_hi;
    if (!wait_bit(I2C_XMIT_DATA_EMPTY)) { if (ok) *ok = 0; return 0xFF; }
    I2C_TX = addr_lo;
    if (!wait_bit(I2C_XMIT_DATA_EMPTY)) { if (ok) *ok = 0; return 0xFF; }
    I2C_STA &= I2C_CLEAR_ALL;

    /* --- read phase: repeated START with R/W=1, dummy trigger --- */
    I2C_SADDR = EEPROM_ADDR_READ;
    /* nLen==1 case: set STOP_READ up front, dummy-trigger the transfer,
     * wait for the byte, done. TI I2c.c:99-108 verbatim. */
    I2C_STA  |= I2C_STOP_READ;
    I2C_TX    = 0xFF;              /* dummy — "kicks" the read */
    if (!wait_bit(I2C_RCV_DATA_FULL)) { if (ok) *ok = 0; return 0xFF; }
    {
        unsigned char v = I2C_RX;
        I2C_STA &= I2C_CLEAR_ALL;
        if (ok) *ok = 1;
        return v;
    }
}

unsigned char eeprom_smoke_test(void)
{
    static const unsigned char SCRATCH_HI = 0x1F;
    static const unsigned char SCRATCH_LO = 0xFF;   /* last byte of 8 KB */
    unsigned char ok;
    unsigned char v;

    if (!eeprom_write_byte(SCRATCH_HI, SCRATCH_LO, 0xA5)) return 0;
    v = eeprom_read_byte(SCRATCH_HI, SCRATCH_LO, &ok);
    if (!ok || v != 0xA5) return 0;

    if (!eeprom_write_byte(SCRATCH_HI, SCRATCH_LO, 0x5A)) return 0;
    v = eeprom_read_byte(SCRATCH_HI, SCRATCH_LO, &ok);
    if (!ok || v != 0x5A) return 0;

    (void)eeprom_write_byte(SCRATCH_HI, SCRATCH_LO, 0xFF);
    return 1;
}

unsigned char eeprom_invalidate_signature(void)
{
    /* Zero the header CHECKSUM at offset 0x0000, and nothing else.
     *
     * eepromExist() reads the header and calls eepromCheckFirmware(); a
     * checksum that does not add up sets dataType = EEPROM_INVALID, which
     * is neither APPCODE nor UNEXIST/DEVICE_TYPE, so RomBoot.c:60-66
     * selects DFU_TARGET_EEPROM. We land in DFU and the EEPROM stays
     * writable.
     *
     * DO NOT go back to zeroing the signature bytes at offsets 2-3. That
     * also reaches DFU, but from that state the boot ROM cannot PROGRAM
     * the EEPROM: block 0 of a download returns errPROG and the ROM then
     * hangs in an I2C wait. Reproduced deterministically twice on
     * 2026-07-28 from a clean dfuIDLE, and it cost an SDA short plus a
     * RAM-resident flasher to recover. The chip itself is fine — a
     * bit-banged write to it succeeded minutes earlier — so it is
     * specifically the boot ROM's dfuEepromCopy that fails once the
     * signature is gone.
     *
     * The checksum rule is sum(header[1..17]) mod 256 == header[0]
     * (Eeprom.c eepromCheckFirmware, "computed by adding bytes"). For the
     * image we ship that sum is 0x96, so writing 0x00 reliably fails it.
     * The function name is kept for callers; what it invalidates is the
     * header, which is what actually matters. */
    if (!eeprom_write_byte(0x00, 0x00, 0x00)) return 0;
    return 1;
}
