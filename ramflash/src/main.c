/*
 * ramflash — write a complete firmware image to EEPROM from program RAM,
 * without involving the boot ROM's EEPROM writer at all.
 *
 * WHY THIS EXISTS
 *
 * On 2026-07-28 the EEPROM signature was zeroed (by sigkill) to reach DFU.
 * That worked — the device cold-booted ffff:fffe. But from that state the
 * boot ROM cannot PROGRAM the EEPROM: a DFU download died on block 0 with
 * errPROG and then hung in an I2C wait, twice, deterministically, from a
 * clean dfuIDLE both times.
 *
 * It is not a hardware fault. sigkill's own bit-banged I2C write to this
 * exact chip succeeded minutes earlier. The chip programs fine; the boot
 * ROM's dfuEepromCopy is what fails. So this image cuts the boot ROM out
 * of the write path and uses only the byte-write sequence that has never
 * failed on this hardware.
 *
 * HOW IT IS USED
 *
 *   1. Short SDA, plug in        -> I2CAccess fails -> EEPROM_UNEXIST
 *                                   -> DFU_TARGET_RAM (RomBoot.c:60-66)
 *   2. DFU-download this image
 *   3. REMOVE THE SHORT before the bus reset that launches it, so I2C
 *      works when it runs
 *   4. It writes the whole image and halts. Device never attaches, and
 *      stays dark for the ~4 minutes the write takes.
 *   5. Replug -> boot ROM reads a valid header -> boots the firmware
 *
 * WRITE ORDER IS THE SAFETY PROPERTY. Payload first, then the header
 * body, then headerSize, then the signature bytes, and the header
 * CHECKSUM absolutely last. eepromExist() rejects a header whose checksum
 * does not add up, so until that final byte lands the device is
 * guaranteed to fall into DFU rather than boot a partial image. An
 * interrupted run is therefore always recoverable, never a brick.
 *
 * DELIBERATELY NO USB, and no progress reporting — it cannot enumerate,
 * so silence is the expected state. The only observable is that it goes
 * dark and stays dark.
 */

#include <mcs51/8051.h>
#include "payload.h"

#define XDATA(a) (*(volatile __xdata unsigned char *)(a))

/* I2C peripheral — same register map and sequence as sigkill and
 * safety_net's eeprom_wr, the only EEPROM write path proven on this
 * hardware (it zeroed the signature on 2026-07-27 and again 2026-07-28). */
#define I2C_STA     XDATA(0xFFC0)
#define I2C_TX      XDATA(0xFFC1)
#define I2C_SADDR   XDATA(0xFFC3)
#define USBCTL      XDATA(0xFFFC)

#define I2C_STOP_WRITE      0x01
#define I2C_XMIT_DATA_EMPTY 0x08
#define I2C_ERROR           0x20
#define I2C_CLEAR_ALL       0x54

#define HEADER_SIZE   18
#define SIG_LO_A      2
#define SIG_LO_B      3
#define CHKSUM_OFF    0

static unsigned char wait_bit(unsigned char mask)
{
    unsigned int t;
    unsigned char s;
    for (t = 0; t < 0xFF00; t++) {
        s = I2C_STA;
        if (s & I2C_ERROR) return 0;
        if (s & mask)      return 1;
    }
    return 0;
}

/* 24C64 program-cycle wait. Same constant as sigkill and mboxfw/src/eeprom.c.
 * Comfortably longer than the datasheet's 5 ms tWR; this image is a one-shot
 * recovery, so the proven constant is worth more than the speed. */
static void write_hold(void)
{
    unsigned int i;
    for (i = 0; i < 0xFF00; i++) { }
}

static unsigned char eeprom_wr(unsigned char hi, unsigned char lo,
                               unsigned char v)
{
    /* TI I2c.c I2CAccess — byte-write sequence: clear status, set slave
     * address, then address bytes high-to-low followed by data. Copied
     * verbatim from sigkill/src/main.c eeprom_wr. 0xA0 is the 24C64 write
     * address; two address bytes = word addressing, which is what an 8 KB
     * part requires and what has been observed to work here. */
    I2C_STA  &= I2C_CLEAR_ALL;
    I2C_SADDR = 0xA0;
    I2C_TX = hi;                    /* address high byte */
    if (!wait_bit(I2C_XMIT_DATA_EMPTY)) return 0;
    /* TI I2c.c I2CAccess — address low byte */
    I2C_TX = lo;
    if (!wait_bit(I2C_XMIT_DATA_EMPTY)) return 0;
    /* TI I2c.c I2CAccess — arm STOP before the final byte, then send data */
    I2C_STA |= I2C_STOP_WRITE;
    I2C_TX = v;
    if (!wait_bit(I2C_XMIT_DATA_EMPTY)) return 0;
    /* TI I2c.c I2CAccess — clear status, then the 24C64 program-cycle wait */
    I2C_STA &= I2C_CLEAR_ALL;
    write_hold();
    return 1;
}

/* Write one image byte at its own offset. Retried once: a single failed
 * byte would otherwise silently corrupt the image, and a retry costs one
 * program cycle against a ~4 minute total. */
static void put(unsigned int off)
{
    unsigned char hi = (unsigned char)(off >> 8);
    unsigned char lo = (unsigned char)(off & 0xFF);
    unsigned char v  = fw_image[off];

    if (!eeprom_wr(hi, lo, v))
        (void)eeprom_wr(hi, lo, v);
}

void main(void)
{
    unsigned int off;
    unsigned int settle;

    EA = 0;                 /* no interrupts, ever, in this image */

    /* Drop off the bus. The boot ROM hands over with USBCTL still holding
     * CONN from the DFU session (TI Utils.SRC UtilResetCPU leaves USBCTL,
     * USBFADR and USBSTA untouched — those writes are commented out), so
     * without this the host keeps talking to a device that will never
     * answer for the several minutes this takes. Intentional assignment to
     * a boot-ROM-owned SFR — POLICY §2 carve-out A, same as sigkill. */
    USBCTL = 0;

    /* Let the I2C lines settle after the SDA short is removed. */
    for (settle = 0; settle < 0xFFFF; settle++) { }

    /* 1. Payload, everything past the header. */
    for (off = HEADER_SIZE; off < fw_len; off++)
        put(off);

    /* 2. Header body, skipping checksum/headerSize/signatures. */
    for (off = 4; off < HEADER_SIZE; off++)
        put(off);

    /* 3. headerSize, then the signature bytes. */
    put(1);
    put(SIG_LO_A);
    put(SIG_LO_B);

    /* 4. Checksum LAST. Until this byte lands, eepromCheckFirmware fails
     * and the boot ROM drops to DFU — which is exactly the behaviour we
     * want from any run that does not reach this line. */
    put(CHKSUM_OFF);

    /* Halt. Do NOT call RESET_TO_BOOT_ROM(): clearing MEMCFG.SDW from
     * program RAM unmaps the running code (TI links UtilResetBootCPU at
     * 0x8003, inside the ROM, precisely so it survives the flip). The
     * replug the user performs next IS the power cycle. */
    for (;;) { }
}
