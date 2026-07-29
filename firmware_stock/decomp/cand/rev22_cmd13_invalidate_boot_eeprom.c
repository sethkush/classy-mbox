// MATCH: image=rev22 addr=0x0517 len=14 func=cmd13_invalidate_boot_eeprom cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 13 (0x0D), Rev 22: make the EEPROM image unbootable, so the next power
 * cycle lands in the boot ROM's DFU instead of running the application.
 * Counterpart of Rev 20's evt0d_invalidate_boot_eeprom at 0x0518.
 *
 * SAFETY-RELEVANT. This is the firmware's own DFU trigger, and it is one-way:
 * after it runs, nothing in the application can undo it, because a power cycle
 * no longer reaches the application. There is no confirmation step and no
 * deferral -- whoever queued event 13 has already decided.
 *
 * WHAT IS ACTUALLY WRITTEN. The call is i2c_eeprom_write3(0, 0, 0): address
 * high = R7 = 0, address low = R5 = 0, data = R3 = 0. The argument registers
 * are pinned by the callee at 0x0BDA, which opens with `MOV R6,0x05` -- IRAM
 * byte 0x05 is bank-0 R5, i.e. it stashes the address low byte -- and then
 * writes R7 to the I2C data register at 0xFFC1 as the first byte on the wire.
 *
 * So the target is EEPROM offset 0. In the 18-byte TAS1020B EEPROM header
 * (tools/wrap_hex.py, mirrored in tools/mboxflash_linux.py:198-201) that is
 * the HEADER CHECKSUM byte, not the signature:
 *
 *     0    chksum          <-- this byte, zeroed here
 *     1    headerSize (18)
 *     2-3  signature 0x12 0x34
 *     4-5  idVendor   6-7  idProduct   8 productVersion
 *     ...  13 wPageSize  14 dataType  15 rPageSize  16-17 payloadSize BE
 *
 * What the boot ROM rejects on the next cold start is therefore a header whose
 * checksum no longer covers its contents. The signature and dataType are left
 * intact, which is what keeps the resulting DFU session pointed at
 * TARGET_EEPROM and so able to reflash (POLICY.md sec. 7). A zeroed *signature*
 * also reaches DFU but leaves the boot ROM unable to write the EEPROM back --
 * that is the failure mode recorded in the project memory as "zeroed signature
 * can't flash". Ghidra's name for this function says "boot signature"; the
 * bytes say offset 0, and offset 0 is the checksum. The Ghidra name is wrong.
 *
 * The trailing OEPDCNTX0 = 0 completes the control transfer that requested it:
 * byte count 0 with the NAK bit clear hands the EP0 OUT X-buffer back to the
 * USB engine. (Bit 7 is the NAK bit -- established by the routine that writes
 * 0x80 to the same register to hold both directions off.) Note only the OUT
 * counter is touched here, where neighbouring handlers clear both.
 *
 * REV 20 -> REV 22 DELTA: BYTE-IDENTICAL EXCEPT ONE RELOCATED CALL OPERAND.
 * rev20 0x0518 and rev22 0x0517 differ only in the LCALL target -- 0x0BEE
 * there, 0x0BDA here. Even the SJMP displacement is the same 0x3E, because the
 * dispatcher epilogue moved by exactly the one byte the handler itself did
 * (rev20 0x0564, rev22 0x0563). The DFU trigger was NOT what Rev 22 changed.
 *
 * NAKED because all three arguments are Keil register parameters -- R7, R5, R3
 * for the first, second and third char -- which SDCC's convention does not
 * use, and because Keil produced all three zeros from a single CLR A. */
void cmd13_invalidate_boot_eeprom(void) __naked {
    __asm
        .globl _i2c_eeprom_write3
        clr   a
        mov   r3,a                 ; data      = 0x00
        mov   r5,a                 ; addr low  = 0x00
        mov   r7,a                 ; addr high = 0x00
        lcall _i2c_eeprom_write3   ; EEPROM[0x0000] = 0 -> header checksum no
                                   ; longer matches; the next cold start stops
                                   ; in the boot ROM DFU
        mov   dptr,#0xffab         ; OEPDCNTX0
        clr   a
        movx  @dptr,a              ; ACK the OUT stage: count 0, NAK clear

        /* `sjmp _evt_dispatch_epilogue`. sdas cannot encode a short jump to an
         * external symbol, so it is written self-relative. `.` is
         * area-relative, so the emitted displacement is the constant 0x3E at
         * assembly time and unchanged by relocation: 0x0523 + 2 + 0x3E =
         * 0x0563. */
        sjmp  . + (0x0563 - 0x0523)   ; -> evt_dispatch_epilogue
    __endasm;
}
