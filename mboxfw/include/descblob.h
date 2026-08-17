#ifndef MBOXFW_DESCBLOB_H
#define MBOXFW_DESCBLOB_H
/*
 * #221 — descriptors provisioned in EEPROM instead of compiled into program RAM.
 *
 * WHY. The TAS1020B executes from 6016 bytes of internal program RAM (datasheet
 * features list, §1 overview, block diagram and both memory maps -- "6016
 * Bytes" at 1780h, stated four ways). The 8192-byte EEPROM behind it is
 * STORAGE: the boot ROM copies the image into that RAM and runs it there, so an
 * image over 6016 bytes has nowhere to live. That is not theoretical -- raising
 * the linker limit produced a 6448-byte image that flashed cleanly, reached DFU
 * manifest and came up silent on USB (BRICK_LOG.md).
 *
 * The descriptor tables are ~426 bytes of that RAM and are PURE DATA: read-only,
 * served on request, never executed. Moving them to the ~2.7 KB of unused EEPROM
 * trades program RAM -- the scarce resource, 5 bytes spare in the diagnostic
 * tier -- for storage nobody is using.
 *
 * READ ONCE AT INIT, NOT PER REQUEST. An earlier sketch streamed each descriptor
 * from EEPROM as the host asked for it. This reads the whole blob into XDATA
 * once (XDATA is 4096 bytes and mostly idle) and serves from there, so the
 * enumeration path keeps exactly the timing it has today and I2C failure has one
 * place to happen instead of one per request.
 *
 * IF THE READ FAILS THE DEVICE STILL ENUMERATES. A minimal built-in fallback
 * lives in program RAM: a device descriptor, a one-interface configuration, and
 * a product string that says so. It carries no audio and is useless for playing
 * anything -- but a unit that enumerates and NAMES its fault is diagnosable from
 * 2 km away, and a silent one is indistinguishable from a brick. That is worth
 * its ~80 bytes on this bench.
 */

/* Base address in EEPROM. The largest image that can exist is 6016 + an 18-byte
 * header = 6034 = 0x1792, so 0x1800 clears any possible payload with 110 bytes
 * to spare and leaves 0x1800..0x1FFF = 2048 bytes for the blob. */
#define EE_DESC_BASE_HI   0x18
#define EE_DESC_BASE_LO   0x00

#define DESC_BLOB_MAGIC0  'M'
#define DESC_BLOB_MAGIC1  'B'
#define DESC_BLOB_MAGIC2  'D'
#define DESC_BLOB_MAGIC3  '1'

/* BUMP THIS WHENEVER THE DESCRIPTOR SET CHANGES SHAPE. The image and the blob
 * are flashed together but can drift apart -- a unit reflashed with a new image
 * over an old blob would enumerate with stale descriptors, which is a new class
 * of field failure and the main cost of this whole scheme. The version byte is
 * what turns that into a clean fallback instead of a wrong device. */
#define DESC_BLOB_VERSION 0x01

/* Header: magic[4], version, n_entries, total_len[2], xor checksum, pad[3] */
#define DESC_HDR_LEN      12
#define DESC_DIR_ENTRY    4          /* offset[2], length[2] */
#define DESC_MAX_ENTRIES  11
#define DESC_BLOB_MAX     560        /* header + directory + ~426 payload */

/* Directory slots, in the order usb.c dispatches them. */
#define DESC_ID_DEVICE    0
#define DESC_ID_CONFIG    1
#define DESC_ID_STR_LANG  2
#define DESC_ID_STR_MFR   3
#define DESC_ID_STR_PROD  4
#define DESC_ID_STR_SER   5
#define DESC_ID_STR_LIN   6
#define DESC_ID_STR_INST  7
#define DESC_ID_STR_SPIN  8
#define DESC_ID_STR_LOUT  9
#define DESC_ID_STR_SPOUT 10

/* Read and validate the blob. Returns 1 if the EEPROM copy is in use, 0 if the
 * built-in fallback is. Call once, early, before usb_init() serves anything. */
unsigned char descblob_load(void);

/* Resolve a slot. Returns a pointer into the XDATA copy and sets *len, or
 * returns 0 for a slot this blob does not carry (so the caller stalls, exactly
 * as it does today for an undeclared string index). */
__xdata unsigned char *descblob_get(unsigned char id, unsigned int *len);

/* 1 when the descriptors came from EEPROM, 0 when the fallback is in use.
 * Reported in telemetry block 0 so a unit can say which it is running. */
extern __bit g_descblob_ok;

#endif
