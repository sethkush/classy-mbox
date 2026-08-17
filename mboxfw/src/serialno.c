/*
 * #221 — iSerialNumber from EEPROM. See serialno.h for why.
 *
 * EVERY BUFFER HERE IS INTERNAL RAM (__data), AND THAT IS #226. As __xdata
 * globals these three arrays were allocated at XDATA 0x0001 -- 87 bytes of
 * address space this board does not implement -- so serialno_load() read the
 * EEPROM correctly and stored the result into a hole. iSerialNumber was
 * therefore never served, from the day #221 shipped, with no I2C fault
 * involved at all. See the note in eeprom.c.
 */
#include "regs.h"
#include "eeprom.h"
#include "usb.h"
#include "serialno.h"

__bit g_serial_ok;


/* 2-byte prefix + UTF-16LE. A USB string descriptor is 16-bit, so the ASCII in
 * EEPROM is expanded here rather than stored wide -- half the EEPROM bytes and
 * half the I2C time, for one shift per character. */
static __idata unsigned char g_str[2 + 2 * SERIAL_MAX_CHARS];
/* g_raw is live only inside serialno_load(), which runs once at boot, so it is
 * a local rather than a static -- SDCC overlays non-reentrant locals, so those
 * bytes are reused by every other function instead of being held for the life
 * of the program. Internal RAM is scarce enough that this matters. */

unsigned char serialno_load(void)
{
    unsigned char n, sum, i;
    __idata unsigned char g_raw[SERIAL_HDR_LEN + SERIAL_MAX_CHARS];

    g_serial_ok = 0;


    /* ONE read of the whole maximum-size record, not a header read followed by
     * a payload read. The two-call form cost 11 bytes more than the image had
     * room for once #224 added the microphone terminal, and it bought nothing:
     * reading 20 bytes that may be padding costs ~2 ms of I2C on a path that
     * runs once at boot, and validating a buffer is the same work either way. */
    if (!eeprom_read_seq(EE_SERIAL_HI, EE_SERIAL_LO, g_raw,
                         SERIAL_HDR_LEN + SERIAL_MAX_CHARS))
        return 0;

    if (g_raw[0] != SERIAL_MAGIC0 || g_raw[1] != SERIAL_MAGIC1 ||
        g_raw[2] != SERIAL_MAGIC2 || g_raw[3] != SERIAL_MAGIC3)
        return 0;
    if (g_raw[4] != SERIAL_VERSION)
        return 0;

    n = g_raw[5];
    if (n == 0 || n > SERIAL_MAX_CHARS)
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
    g_serial_ok = 1;
    return 1;
}

__idata unsigned char *serialno_string(unsigned int *len)
{
    if (!g_serial_ok) return 0;
    *len = g_str[0];
    return g_str;
}

