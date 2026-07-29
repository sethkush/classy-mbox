// MATCH: image=rev20 addr=0x0518 len=14 func=evt0d_invalidate_boot_eeprom cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 13 (0x0D): make the EEPROM image unbootable, so the next power cycle
 * lands in the boot ROM's DFU instead of running the application.
 *
 * SAFETY-RELEVANT. This is the firmware's own DFU trigger, and it is
 * one-way: after it runs, nothing in the application can undo it, because a
 * power cycle no longer reaches the application.
 *
 * WHAT IS ACTUALLY WRITTEN. The call is i2c_eeprom_write_byte(0, 0, 0):
 * address high = R7 = 0, address low = R5 = 0, data = R3 = 0. That is EEPROM
 * offset 0, which in the 18-byte TAS1020B EEPROM header is the *header
 * checksum* byte -- not the signature. The header layout (tools/wrap_hex.py,
 * mirrored in tools/mboxflash_linux.py:198-201) is
 *
 *     0    chksum          <-- this byte, zeroed here
 *     1    headerSize (18)
 *     2-3  signature 0x12 0x34
 *     4-5  idVendor   6-7  idProduct   8 productVersion
 *     ...  13 wPageSize  14 dataType  15 rPageSize  16-17 payloadSize BE
 *
 * so what the boot ROM rejects on the next cold start is a header whose
 * checksum no longer covers its contents. The signature and dataType are left
 * intact, which is what keeps the resulting DFU session pointed at
 * TARGET_EEPROM and therefore able to reflash (POLICY.md sec. 7): a zeroed
 * *signature* also reaches DFU but leaves the boot ROM unable to write the
 * EEPROM back. Ghidra's name for this function says "boot signature"; the
 * bytes say offset 0, and offset 0 is the checksum.
 *
 * The write is not deferred and there is no confirmation step. Whoever queued
 * event 13 has already decided.
 *
 * The trailing OEPDCNTX0 = 0 completes the control transfer that requested it:
 * byte count 0 with the NAK bit clear hands the EP0 OUT X-buffer back to the
 * USB engine. (Bit 7 is the NAK bit -- established by ep0_nack_both at 0x0B5F,
 * which writes 0x80 to the same register to hold both directions off.) Note
 * only the OUT counter is touched here, where the neighbouring handlers call
 * ep0_arm_zlp at 0x0FEA and clear both.
 *
 * REV 22 CROSS-CHECK. The same handler is cmd13_invalidate_boot_eeprom at
 * rev22 0x0517 and is byte-identical except for the relocated call target
 * (0x0BDA there against 0x0BEE here); even the SJMP displacement is the same
 * 0x3E, because Rev 22's dispatcher epilogue moved by exactly the one byte the
 * handler itself did (rev20 0x0564, rev22 0x0563).
 *
 * NAKED because all three arguments are Keil register parameters -- R7, R5,
 * R3 for the first, second and third char -- which SDCC's convention does not
 * use, and because Keil produced all three zeros from a single CLR A. */
void evt0d_invalidate_boot_eeprom(void) __naked {
    __asm
        .globl _i2c_eeprom_write_byte
        clr   a
        mov   r3,a                 ; data      = 0x00
        mov   r5,a                 ; addr low  = 0x00
        mov   r7,a                 ; addr high = 0x00
        lcall _i2c_eeprom_write_byte   ; EEPROM[0x0000] = 0 -> header checksum
                                       ; no longer matches; next cold start
                                       ; stops in the boot ROM DFU
        mov   dptr,#0xffab         ; OEPDCNTX0
        clr   a
        movx  @dptr,a              ; ACK the OUT stage: count 0, NAK clear

        /* `sjmp _evt_dispatch_epilogue`. sdas cannot encode a short jump to an
         * external symbol -- it emits 80 00 and leaves the displacement to the
         * linker -- so it is written self-relative. `.` is this instruction's
         * address and is area-relative, so the difference is a constant 0x40
         * at assembly time and unchanged by relocation: 0x0524 + 2 + 0x3E =
         * 0x0564. See cmd12_set_cpt_mode1.c for the same construct. */
        sjmp  . + (0x0564 - 0x0524)   ; -> evt_dispatch_epilogue
    __endasm;
}
