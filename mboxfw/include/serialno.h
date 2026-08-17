#ifndef MBOXFW_SERIALNO_H
#define MBOXFW_SERIALNO_H
/*
 * #221 — iSerialNumber provisioned in EEPROM, read at boot.
 *
 * WHY NOT A COMPILE-TIME CONSTANT. #193 settled that the default image serves
 * iSerialNumber 0, and the reason still stands: one image is flashed to any
 * number of units, so a baked-in string would make every one of them claim the
 * SAME serial. Hosts key device identity on that field -- Windows builds its
 * device-instance path from it -- so two units with one serial collide in the
 * device database instead of appearing as two devices. "No serial" is a legal,
 * honest statement; "the same serial as every other unit" is a false one.
 * MBOX_UNIT=A/B exists for the bench and must never ship.
 *
 * #193 also said the option worth wanting was a runtime source, and that there
 * wasn't one: the serials are label-only, typed into usb.h by hand, present in
 * no stock image or EEPROM dump. This provides that source -- the flasher
 * writes the string printed on the unit into spare EEPROM, and one image then
 * serves the true serial on every device.
 *
 * WHY THIS AND NOT THE FULL DESCRIPTOR MOVE. Moving ALL descriptors to EEPROM
 * was measured and MADE THE IMAGE 658 BYTES BIGGER: the tables are 426 bytes,
 * the blob directory, validation, bounds checks, XDATA staging and fallback
 * descriptor set cost ~1084. The serial needs almost none of that -- one
 * record, no directory, no fallback set, two dispatch cases instead of eleven.
 *
 * IF THE RECORD IS ABSENT OR BAD, iSerialNumber IS 0. Exactly today's
 * behaviour, and the only safe answer: #193 records that an index pointing at
 * a string that is not there makes the host ask for string 3 and get a STALL
 * mid-enumeration, which some hosts shrug off and some abandon the device over.
 * So the index and the string are decided together, from one flag, and can
 * never disagree.
 */

/* Last 256 bytes of the 8 KB EEPROM. The largest image that can exist is
 * 6016 + an 18-byte header = 6034 = 0x1792, so 0x1F00 clears any possible
 * payload by a wide margin. */
#define EE_SERIAL_HI      0x1F
#define EE_SERIAL_LO      0x00

#define SERIAL_MAGIC0     'M'
#define SERIAL_MAGIC1     'B'
#define SERIAL_MAGIC2     'S'
#define SERIAL_MAGIC3     'N'
#define SERIAL_VERSION    0x01
/* 12, not 20. Every persistent byte here is internal RAM now (see the #226
 * note in eeprom.c on why __xdata is unusable), and internal RAM is shared with
 * the stack. The string descriptor costs 2 + 2*N bytes, so 20 chars is 42 and
 * 12 is 26. Both real unit serials are 11 and 10 characters -- RK10874600Q and
 * RK1672500M -- so 12 keeps headroom without spending it on capacity nothing
 * uses. A longer serial is REJECTED at provisioning time by mboxprov.py, which
 * reads this constant, rather than silently truncated. */
#define SERIAL_MAX_CHARS  12
#define SERIAL_HDR_LEN    7      /* magic[4], version, nchar, xor */

/* Read and validate the record; build the USB string descriptor. Returns 1 if
 * a serial is being served. Call once, before usb_init(). */
unsigned char serialno_load(void);

/* The built string descriptor, or 0 if none. Sets *len when it returns non-0.
 * INTERNAL RAM -- see the #226 note in eeprom.c. */
__idata unsigned char *serialno_string(unsigned int *len);

/* #226: serialno_devdesc() is GONE. The two device-descriptor variants live in
 * __code (descriptors.c, AppDevDesc / AppDevDescSN) and usb.c picks between
 * them on g_serial_ok. An 18-byte RAM duplicate for one differing byte cost
 * stack this part cannot spare. */

extern __bit g_serial_ok;

#endif
