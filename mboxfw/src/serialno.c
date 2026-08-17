/*
 * #221 — iSerialNumber from EEPROM. See serialno.h for why.
 */
#include "regs.h"
#include "eeprom.h"
#include "usb.h"
#include "serialno.h"

__bit g_serial_ok;

extern const __code unsigned char AppDevDesc[];

/* 2-byte prefix + UTF-16LE. A USB string descriptor is 16-bit, so the ASCII in
 * EEPROM is expanded here rather than stored wide -- half the EEPROM bytes and
 * half the I2C time, for one shift per character. */
static __xdata unsigned char g_str[2 + 2 * SERIAL_MAX_CHARS];
static __xdata unsigned char g_dev[APP_DEV_DESC_LEN];
static __xdata unsigned char g_raw[SERIAL_HDR_LEN + SERIAL_MAX_CHARS];

unsigned char serialno_load(void)
{
    unsigned char n, sum, i;

    g_serial_ok = 0;

    /* Copy the device descriptor up front, whatever happens to the record.
     * Serving it from XDATA in BOTH cases keeps one path through the dispatch
     * instead of two, and the only difference between them is one byte. */
    for (i = 0; i < APP_DEV_DESC_LEN; i++)
        g_dev[i] = AppDevDesc[i];
    g_dev[16] = 0;                      /* iSerialNumber, until proven otherwise */

    if (!eeprom_read_seq(EE_SERIAL_HI, EE_SERIAL_LO, g_raw, SERIAL_HDR_LEN))
        return 0;

    if (g_raw[0] != SERIAL_MAGIC0 || g_raw[1] != SERIAL_MAGIC1 ||
        g_raw[2] != SERIAL_MAGIC2 || g_raw[3] != SERIAL_MAGIC3)
        return 0;
    if (g_raw[4] != SERIAL_VERSION)
        return 0;

    n = g_raw[5];
    if (n == 0 || n > SERIAL_MAX_CHARS)
        return 0;

    /* Re-read header AND payload in one run. Reading the characters separately
     * would leave a window where the length came from one transaction and the
     * data from another -- harmless here, but the whole record is 27 bytes and
     * one read is simpler to reason about than two. */
    if (!eeprom_read_seq(EE_SERIAL_HI, EE_SERIAL_LO, g_raw,
                         (unsigned int)(SERIAL_HDR_LEN + n)))
        return 0;

    /* XOR over the record with the checksum byte itself excluded. This is
     * guarding against a half-written or truncated record -- a flash
     * interrupted partway -- not against an adversary. */
    sum = 0;
    for (i = 0; i < SERIAL_HDR_LEN + n; i++) {
        if (i != 6) sum ^= g_raw[i];
    }
    if (sum != g_raw[6])
        return 0;

    /* Reject anything that is not printable ASCII. A serial is compared and
     * displayed by humans and written into host device databases; letting a
     * stray 0x00 or 0xFF through would produce a string descriptor that is
     * technically well-formed and useless, and it is a one-line check. */
    for (i = 0; i < n; i++) {
        unsigned char c = g_raw[SERIAL_HDR_LEN + i];
        if (c < 0x20 || c > 0x7E) return 0;
    }

    g_str[0] = (unsigned char)(2 + 2 * n);
    g_str[1] = USB_DT_STRING;
    for (i = 0; i < n; i++) {
        g_str[2 + 2 * i]     = g_raw[SERIAL_HDR_LEN + i];
        g_str[2 + 2 * i + 1] = 0;
    }

    /* Index and string are set together, from this one point, so they cannot
     * disagree -- see serialno.h on why a dangling index is worse than none. */
    g_dev[16] = 3;
    g_serial_ok = 1;
    return 1;
}

__xdata unsigned char *serialno_string(unsigned int *len)
{
    if (!g_serial_ok) return 0;
    *len = g_str[0];
    return g_str;
}

__xdata unsigned char *serialno_devdesc(void)
{
    return g_dev;
}
