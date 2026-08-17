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

/*
 * Invalidate the EEPROM header signature bytes at offset 0x0002-0x0003
 * (the 0x12 0x34 marker the boot ROM checks). Writes 0x00 to each.
 * Returns 1 on success, 0 if any write failed. The next power-on will
 * see a bad signature and drop to boot-ROM DFU (0xFFFF:0xFFFE).
 *
 * ONLY call this AFTER eeprom_smoke_test() returns 1 — otherwise a
 * partial-write could half-corrupt the header in an unrecoverable way.
 */
#if defined(MBOX_SERIAL_EEPROM) || defined(MBOX_PROVISION)
/* #221: sequential read for the descriptor blob. One START for the whole run;
 * see the note in eeprom.c on why this is not a loop of eeprom_read_byte(). */
/* len >= 2 required; anything less returns 0 (failure), not a short read.
 * See the note in eeprom.c -- the record is 27 bytes and no caller wants one. */
unsigned char eeprom_read_seq(unsigned char addr_hi,
                              unsigned char addr_lo,
                              __xdata unsigned char *dst,
                              unsigned char len);

#endif

#ifdef MBOX_PROVISION
/* #226 diagnostic: one instrumented byte read, 8 bytes of I2C status out.
 * Provisioning builds only -- see the comment in eeprom.c. */
void eeprom_read_diag(unsigned char addr_hi, unsigned char addr_lo,
                      __data unsigned char *out, unsigned char style);
#endif

unsigned char eeprom_invalidate_signature(void);

#endif
