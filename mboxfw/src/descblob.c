/*
 * #221 — load the descriptor blob from EEPROM into XDATA. See descblob.h for
 * why this exists and what the failure story is.
 */
#include "regs.h"
#include "eeprom.h"
#include "descblob.h"
#include "usb.h"

__bit g_descblob_ok;

static __xdata unsigned char g_blob[DESC_BLOB_MAX];

/* THE FALLBACK, in program RAM, used only when the EEPROM copy is unusable.
 *
 * It is deliberately the smallest thing that still enumerates and can be
 * identified: a device descriptor pointing at one string, and a configuration
 * with a single vendor-class interface and no endpoints. A host will attach it,
 * find nothing to play, and show the product string -- which says exactly what
 * went wrong. The alternative is a device that answers nothing, which from 2 km
 * away is indistinguishable from a brick and provokes a trip.
 *
 * bMaxPacketSize0 is 8 and bcdUSB 0x0110, matching the real descriptors, so the
 * boot-time behaviour a host sees is identical up to the point of failure. */
static const __code unsigned char FallbackDev[18] = {
    18, USB_DT_DEVICE, 0x10, 0x01, 0x00, 0x00, 0x00, 8,
    (unsigned char)(MBOX_VID & 0xFF), (unsigned char)(MBOX_VID >> 8),
    (unsigned char)(MBOX_PID & 0xFF), (unsigned char)(MBOX_PID >> 8),
    0x00, 0x01, 0, 2, 0, 1
};
static const __code unsigned char FallbackCfg[18] = {
    9, USB_DT_CONFIG, 18, 0, 1, 1, 0, 0x80, 250 / 2,
    9, USB_DT_INTERFACE, 0, 0, 0, 0xFF, 0x00, 0x00, 0
};
static const __code unsigned char FallbackLang[4] = {
    4, USB_DT_STRING, 0x09, 0x04
};
/* "MBOX DESC FAIL" */
static const __code unsigned char FallbackProd[30] = {
    30, USB_DT_STRING,
    'M',0,'B',0,'O',0,'X',0,' ',0,'D',0,'E',0,'S',0,'C',0,' ',0,
    'F',0,'A',0,'I',0,'L',0
};

/* Directory of the fallback, in the same shape descblob_get() returns. */
static __xdata unsigned char *fallback_of(unsigned char id, unsigned int *len)
{
    const __code unsigned char *src = 0;
    unsigned int n = 0, i;

    switch (id) {
        case DESC_ID_DEVICE:   src = FallbackDev;  n = 18; break;
        case DESC_ID_CONFIG:   src = FallbackCfg;  n = 18; break;
        case DESC_ID_STR_LANG: src = FallbackLang; n = 4;  break;
        case DESC_ID_STR_PROD: src = FallbackProd; n = 30; break;
        default: return 0;
    }
    /* Copy into the same XDATA buffer the EEPROM path uses, so there is ONE
     * serving path rather than two. The buffer is free in this branch by
     * definition -- the EEPROM read either failed or was never valid. */
    for (i = 0; i < n; i++) g_blob[i] = src[i];
    *len = n;
    return g_blob;
}

unsigned char descblob_load(void)
{
    unsigned int total, i;
    unsigned char sum;

    g_descblob_ok = 0;

    /* Header first, so a blank or foreign EEPROM costs 12 bytes of I2C rather
     * than a full-length read of nothing. */
    if (!eeprom_read_seq(EE_DESC_BASE_HI, EE_DESC_BASE_LO, g_blob, DESC_HDR_LEN))
        return 0;

    if (g_blob[0] != DESC_BLOB_MAGIC0 || g_blob[1] != DESC_BLOB_MAGIC1 ||
        g_blob[2] != DESC_BLOB_MAGIC2 || g_blob[3] != DESC_BLOB_MAGIC3)
        return 0;

    /* A version mismatch is NOT a soft warning. It means the image and the blob
     * were flashed at different times and the descriptors may describe a device
     * this firmware is not. Falling back is the only safe answer. */
    if (g_blob[4] != DESC_BLOB_VERSION)
        return 0;

    if (g_blob[5] == 0 || g_blob[5] > DESC_MAX_ENTRIES)
        return 0;

    total = (unsigned int)g_blob[6] | ((unsigned int)g_blob[7] << 8);
    if (total < DESC_HDR_LEN || total > DESC_BLOB_MAX)
        return 0;

    if (!eeprom_read_seq(EE_DESC_BASE_HI, EE_DESC_BASE_LO, g_blob, total))
        return 0;

    /* XOR over everything except the checksum byte itself. Cheap, and it is
     * guarding against a truncated or half-written blob rather than against an
     * adversary -- the failure this catches is a flash interrupted partway. */
    sum = 0;
    for (i = 0; i < total; i++) {
        if (i != 8) sum ^= g_blob[i];
    }
    if (sum != g_blob[8])
        return 0;

    g_descblob_ok = 1;
    return 1;
}

__xdata unsigned char *descblob_get(unsigned char id, unsigned int *len)
{
    unsigned int off, n, dir;

    if (!g_descblob_ok)
        return fallback_of(id, len);

    if (id >= g_blob[5])
        return 0;

    dir = DESC_HDR_LEN + (unsigned int)id * DESC_DIR_ENTRY;
    off = (unsigned int)g_blob[dir]     | ((unsigned int)g_blob[dir + 1] << 8);
    n   = (unsigned int)g_blob[dir + 2] | ((unsigned int)g_blob[dir + 3] << 8);

    /* A zero length is how the blob says "this slot is not present" -- that is
     * how a build with no serial provisioned expresses it, and the caller turns
     * it into the same stall it issues today. */
    if (n == 0)
        return 0;

    /* Bounds-check against the blob we actually read. A directory entry that
     * points outside it means a corrupt blob that passed the checksum, which
     * should be impossible -- but serving from beyond the buffer would hand the
     * host whatever XDATA follows, and #211 is a fresh reminder of what running
     * off the end of a buffer looks like on the wire. */
    off += DESC_HDR_LEN + (unsigned int)g_blob[5] * DESC_DIR_ENTRY;
    if (off >= DESC_BLOB_MAX || n > DESC_BLOB_MAX || off + n > DESC_BLOB_MAX)
        return 0;

    *len = n;
    return &g_blob[off];
}
