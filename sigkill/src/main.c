/*
 * sigkill — zero the EEPROM header signature, then halt. Nothing else.
 *
 * WHY THIS EXISTS
 *
 * Reaching DFU on the TAS1020B means making the boot ROM pick
 * DFU_TARGET_EEPROM, which it does whenever the EEPROM header's dataType is
 * neither APPCODE nor UNEXIST/DEVICE_TYPE (RomBoot.c:60-66). Zeroing the two
 * signature bytes makes eepromExist() fail its check, and because it wipes the
 * header struct to zero first, dataType is left at 0x00 -> TARGET_EEPROM.
 *
 * Normally the running firmware does that itself. On 2026-07-28 mboxfw was
 * flashed WITHOUT a working escape hatch — its boot-time button path did not
 * fire, and the over-the-wire trigger had been stripped — so the device booted
 * a valid image with no way back to DFU. This image is the recovery.
 *
 * HOW IT IS USED
 *
 * A real SDA short makes I2CAccess() fail, so eepromExist() sets dataType =
 * EEPROM_UNEXIST and the boot ROM selects DFU_TARGET_RAM: downloads are copied
 * into program RAM and executed, never written to EEPROM. That is exactly what
 * we want here — this image is RUN, not flashed.
 *
 *   1. Short SDA, plug in            -> ffff:fffe, genuinely RAM-target
 *   2. DFU-download this image
 *   3. REMOVE THE SHORT before the bus reset that launches it, so I2C works
 *      when it runs
 *   4. It zeroes the signature and halts (device never attaches — expected)
 *   5. Replug -> dataType 0x00 -> TARGET_EEPROM DFU -> flash normally
 *
 * If the short is still on at step 4 the I2C writes fail, the device still
 * halts, and nothing is harmed — just repeat from step 1.
 *
 * DELIBERATELY NO USB. It cannot enumerate, so it cannot get stuck answering
 * the host; and a halt with no USB is unambiguous — if the device is silent,
 * it ran. Code budget is a few hundred bytes so the generated assembly can be
 * eyeballed in full.
 */

#include <mcs51/8051.h>

#define XDATA(a) (*(volatile __xdata unsigned char *)(a))

/* I2C peripheral — same register map and sequence as safety_net's eeprom_wr,
 * which is the only EEPROM write path proven on this hardware (it is what
 * zeroed the signature on 2026-07-27). */
#define I2C_STA     XDATA(0xFFC0)
#define I2C_TX      XDATA(0xFFC1)
#define I2C_SADDR   XDATA(0xFFC3)
#define USBCTL      XDATA(0xFFFC)

#define I2C_STOP_WRITE      0x01
#define I2C_XMIT_DATA_EMPTY 0x08
#define I2C_ERROR           0x20
#define I2C_CLEAR_ALL       0x54

/* EEPROM header layout (tools/wrap_hex.py): byte 0 chksum, 1 headerSize,
 * 2-3 signatures {0x12, 0x34}. Zeroing 2 and 3 is the minimum that makes
 * eepromExist() reject the header. */
#define SIG_OFFSET_HI  0x00
#define SIG_OFFSET_LO_A 0x02
#define SIG_OFFSET_LO_B 0x03

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

/* 24C64 page-write settle time. ~7 ms at 12 MHz, matching safety_net's
 * eeprom_write_hold and mboxfw/src/eeprom.c. */
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
     * verbatim from safety_net/src/main.c eeprom_wr, the only EEPROM write
     * path proven on this hardware (it zeroed the signature 2026-07-27).
     * 0xA0 is the 24C64 write address. */
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

void main(void)
{
    unsigned int settle;

    EA = 0;                 /* no interrupts, ever, in this image */

    /* Drop off the bus. The boot ROM hands over with USBCTL still holding
     * CONN from the DFU session (TI Utils.SRC UtilResetCPU leaves USBCTL,
     * USBFADR and USBSTA untouched — those writes are commented out), so
     * without this the host keeps talking to a device that will never answer.
     * Intentional assignment to a boot-ROM-owned SFR — POLICY §2 carve-out A,
     * the same one mboxfw's main() uses. */
    USBCTL = 0;

    /* Let the I2C lines settle after the SDA short is removed. */
    for (settle = 0; settle < 0xFFFF; settle++) { }

    /* Best effort, twice each: a failed write leaves the signature intact and
     * simply means "try again", which costs one replug and no damage. */
    (void)eeprom_wr(SIG_OFFSET_HI, SIG_OFFSET_LO_A, 0x00);
    (void)eeprom_wr(SIG_OFFSET_HI, SIG_OFFSET_LO_B, 0x00);
    (void)eeprom_wr(SIG_OFFSET_HI, SIG_OFFSET_LO_A, 0x00);
    (void)eeprom_wr(SIG_OFFSET_HI, SIG_OFFSET_LO_B, 0x00);

    /* Halt. Do NOT call RESET_TO_BOOT_ROM(): clearing MEMCFG.SDW from program
     * RAM unmaps the running code (TI links UtilResetBootCPU at 0x8003, inside
     * the ROM, precisely so it survives the flip). Halting is correct and
     * honest — the device stays dark, and the replug the user performs next IS
     * the power cycle the sequence needs. */
    for (;;) { }
}
