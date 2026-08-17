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
    /* `i` MUST be volatile -- see the note in hw_init.c short_delay(). Without
     * it SDCC deleted this call from eeprom_write_byte() entirely, so every
     * write returned without waiting for the program cycle. eeprom_smoke_test()
     * writes and reads back three times in a row, so its result -- reported as
     * a hardware fault in telemetry block 4 byte 0 -- could be this bug rather
     * than the part. eeprom_invalidate_signature() writes a single byte and is
     * followed by a power cycle, so the DFU path was not affected. */
    volatile unsigned int i;
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

#if defined(MBOX_SERIAL_EEPROM) || defined(MBOX_PROVISION)
/* Sequential read: one START, one word-address load, then `len` bytes clocked
 * out back-to-back. Returns 1 on success, 0 on any failure (and the caller must
 * treat a partial read as a failure -- `dst` is left in an undefined state).
 *
 * WHY NOT A LOOP OF eeprom_read_byte(). That works and is what the first sketch
 * did, but each call re-sends START, both word-address bytes and STOP, so a byte
 * costs ~30 bit times instead of ~9. Over the ~430-byte descriptor blob that is
 * the difference between roughly 130 ms and 40 ms of boot time. The 24C64
 * auto-increments its internal address pointer across reads, which is exactly
 * what makes the short form legal -- see the 24C64 datasheet's sequential read.
 *
 * REWRITTEN 2026-08-16 against I2c.c, after every read returned 0x00 -- see the
 * comment on the dummy write below. The first version was written from a
 * paraphrase of the reference rather than its control flow, which is the same
 * root cause POLICY.md records for wrap_hex.py and #226 records for
 * mkserial.py. It had never been exercised against known data: #221 shipped the
 * read side months before any EEPROM record existed to read. */
unsigned char eeprom_read_seq(unsigned char addr_hi,
                              unsigned char addr_lo,
                              __xdata unsigned char *dst,
                              unsigned char len)
{
    unsigned char i;

    /* len is `unsigned char`, and the single-byte case TI carries is NOT
     * reproduced: both callers read the serial record, 8 bytes (the
     * provisioning readback) or 27 (serialno_load), so neither the 16-bit
     * counter nor the len==1 branch ever ran. Together they cost 63 bytes
     * against 27 of headroom in the shipping image. A caller asking for fewer
     * than 2 gets a FAILURE, not a short read -- callers already treat 0 as
     * "no serial", which is the safe answer, and a silent partial read is the
     * thing eeprom.h's contract warns against. */
    if (len < 2) return 0;

    /* TI I2c.c I2CAccess line 57 — clear stale flags, then the write address. */
    I2C_STA   &= I2C_CLEAR_ALL;
    I2C_SADDR  = EEPROM_ADDR_WRITE;

    /* TI I2c.c I2CAccess line 63 — word-address MSB. */
    I2C_TX = addr_hi;
    if (!wait_bit(I2C_XMIT_DATA_EMPTY)) return 0;
    /* TI I2c.c I2CAccess line 76 — word-address LSB. */
    I2C_TX = addr_lo;
    if (!wait_bit(I2C_XMIT_DATA_EMPTY)) return 0;

    /* TI I2c.c I2CAccess line 82 — CLEAR_ALL between the sub-address and the
     * read address. Omitted in the first version of this function. */
    I2C_STA &= I2C_CLEAR_ALL;

    /* TI I2c.c I2CAccess line 96 — repeated START in READ mode. The 24C64
     * auto-increments from here, which is what makes the run below legal. */
    I2C_SADDR = EEPROM_ADDR_READ;

    /* TI I2c.c I2CAccess line 111 — ONE dummy write, OUTSIDE the loop.
     *
     * THIS IS THE BUG THAT MADE EVERY SEQUENTIAL READ RETURN ZEROS. The first
     * version put this write inside the loop, citing this same line, on the
     * reading that the dummy byte "fires each read cycle". It does not: it
     * fires the FIRST one only. What advances the peripheral to the next byte
     * is READING I2CDATI (line 140) -- TI's loop contains no write to I2CDATO
     * on the read path at all. Re-writing it per iteration restarts the
     * transfer each time, and the reads came back 0x00.
     *
     * The symptom was diagnostic and nearly missed: an UNWRITTEN 24C64 reads
     * 0xFF, so a region reading 0x00 before anything had ever been written to
     * it was already saying the read path was wrong. It was briefly read as
     * "the writes are failing" instead, which fits the after-write evidence
     * and not the before-write evidence. */
    I2C_TX = 0xFF;

    /* TI I2c.c I2CAccess lines 116-140. STOP_READ is armed when TWO bytes
     * remain, AFTER the wait and BEFORE reading I2CDATI, so the STOP lands on
     * the last byte -- TI's comment at line 131 is explicit about this. The
     * first version armed it when ONE remained and before the wait, i.e. a
     * byte late and on the wrong side of the handshake. */
    for (i = len; i != 0; i--) {
        if (!wait_bit(I2C_RCV_DATA_FULL)) return 0;
        /* TI I2c.c I2CAccess line 136 — armed with TWO bytes left, so the STOP
         * lands on the last one. */
        if (i == 2) I2C_STA |= I2C_STOP_READ;
        /* TI I2c.c I2CAccess line 140 — this read is what clocks the next
         * byte; there is no write to I2CDATO anywhere in TI's read loop. */
        *dst++ = I2C_RX;
    }

    /* TI I2c.c I2CAccess line 163 — leave the flags clean for the next
     * transaction. */
    I2C_STA &= I2C_CLEAR_ALL;
    return 1;
}

#endif /* MBOX_SERIAL_EEPROM || MBOX_PROVISION */

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
