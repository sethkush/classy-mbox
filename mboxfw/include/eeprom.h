#ifndef MBOXFW_EEPROM_H
#define MBOXFW_EEPROM_H

/*
 * TAS1020A hardware-I²C peripheral driver for the 24C64 EEPROM at slave
 * address 0x50 (7-bit). Used ONLY by the enter-DFU trigger path to
 * invalidate the EEPROM header signature bytes so the boot ROM drops to
 * DFU mode on the next reset instead of re-loading mboxfw.
 *
 * Bulletproof-recovery contract:
 *   - eeprom_write_byte returns YES on success, NO on any timeout or
 *     peripheral failure. Callers must never assume success.
 *   - eeprom_read_byte returns 0xFF and sets *ok=NO on failure so a
 *     stale byte can't be mistaken for a successful roundtrip.
 *   - The invalidation path in usb.c uses a scratch-byte round-trip
 *     BEFORE touching the signature, so a broken I²C driver falls
 *     back gracefully to the warm-reset path (never worse than the
 *     current soft-brick state).
 */

/* Return 1 on write completion, 0 on timeout. */
unsigned char eeprom_write_byte(unsigned char addr_hi,
                                unsigned char addr_lo,
                                unsigned char data);

/* Return the read byte if ok. *ok = 1 on success, 0 on failure. */
unsigned char eeprom_read_byte(unsigned char addr_hi,
                               unsigned char addr_lo,
                               unsigned char *ok);

/*
 * Smoke-test the I²C driver by writing 0xA5 then 0x5A to the last byte
 * of EEPROM (offset 0x1FFF — past our firmware image) and reading each
 * back. Returns 1 iff both round-trips match. Restores the byte to
 * 0xFF on completion. Caller uses this to decide whether it's safe to
 * touch the header signature.
 */
unsigned char eeprom_smoke_test(void);

/*
 * Invalidate the EEPROM header signature bytes at offset 0x0002-0x0003
 * (the 0x12 0x34 marker the boot ROM checks). Writes 0x00 to each.
 * Returns 1 on success, 0 if any write failed. The next power-on will
 * see a bad signature and drop to boot-ROM DFU (0xFFFF:0xFFFE).
 *
 * ONLY call this AFTER eeprom_smoke_test() returns 1 — otherwise a
 * partial-write could half-corrupt the header in an unrecoverable way.
 */
unsigned char eeprom_invalidate_signature(void);

#endif
