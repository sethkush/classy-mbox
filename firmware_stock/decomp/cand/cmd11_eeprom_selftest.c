// MATCH: image=rev20 addr=0x04C4 len=77 func=cmd11_eeprom_selftest cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 11: prove the EEPROM is writable, and show the answer on the panel.
 *
 * WHAT IT DOES, IN ORDER
 *   1. If the external chips have not been brought up yet, bring them up.
 *   2. Set f_force, go to clock mode 3 (48 kHz), and put the CS8427 in the
 *      state the mode-1 path uses (register 4 = 0x41).
 *   3. Read EEPROM[0x1FFF], write back its ones-complement, read it again.
 *   4. If the read-back equals the complement, the write took: clear the panel
 *      bit at IRAM 0x22.6 and shift chain A out, so the result is visible.
 *   5. Write CS8427 register 0x12 = 0x00 and leave.
 *
 * IRAM 0x22.6 IS NOT THIS FUNCTION'S BIT. It is bit 6 of the front-panel
 * chain-A latch byte and it has three writers in each image; the full account
 * lives in shiftreg8_commit.c, which is the owner. In short: nothing in either
 * image ever reads the bit, and its nominal value is !(f_spdif | f_force), as
 * the two button state machines recompute at rev20 0x0E52-0x0E60 and
 * 0x0EC5-0x0ED3. This function sets f_force at 0x04CA, so by that rule the bit
 * already belongs clear; clearing it on PASS brings the latch into agreement,
 * and a FAIL leaves the latch disagreeing. "PASS indicator" is a description
 * of that one branch, not a claim about what the bit means -- the same output
 * line is driven by cmd4 (0x0454) and cmd5 (0x0466) for an unrelated reason.
 *
 * THIS TEST IS DESTRUCTIVE AND IT DOES NOT RESTORE. EEPROM[0x1FFF] -- the last
 * byte of the 8 KB device -- is left holding the complement of whatever it
 * held before, and each invocation flips it again.
 *
 * 0x1FFF is NOT outside the flashed image. The flasher's budget is the whole
 * 8 KB part (EEPROM_BUDGET = 8192, checked as 18 + payload_size at
 * tools/mboxflash_linux.py:215-216), so a full-budget image is 18 header bytes
 * at 0x0000 plus payload at 0x0012..0x1FFF -- 0x1FFF is its LAST byte. The
 * stock Rev 20 EEPROM is exactly that: firmware_stock/rev20_eeprom.bin is 8192
 * bytes and its header declares payload size 0x1FEE = 8174 at offset 0x0E, so
 * EEPROM 0x1FFF is code address 0x1FED, inside the declared payload.
 *
 * What makes the flip harmless in this image is content, not extent: Rev 20's
 * last non-0xFF code byte is at 0x103E, and everything from 0x103F to the end
 * of the payload is 0xFF erase-fill that nothing executes or reads. (Rev 22 is
 * the same shape with its last non-0xFF byte at 0x1035.) So the test toggles a
 * fill byte here -- but an image that actually used its full budget would have
 * this handler corrupting its own last byte, and it is in any case a real
 * write to real EEPROM with real wear.
 *
 * BIT-VS-BYTE, TWICE, ON THE SAME PRINTED NUMBERS. `JB 0x2E` at 0x04C4 tests
 * BIT 0x2E = IRAM 0x25.6; `SETB 0x2D` at 0x04CA sets BIT 0x2D = IRAM 0x25.5
 * (f_force). Six bytes later `MOV 0x2D,#0x41` writes IRAM BYTE 0x2D, which is
 * a different location entirely -- it is the value half of the register/value
 * pair at bytes 0x2C/0x2D that cs8427_ctl_write's callers stage. Both spellings
 * appear in this one function, four instructions apart.
 *
 * The same doubling exists in the callee: audio_path_reconfig_ext_chips does
 * `SETB 0x2E` at 0x0810 and then `MOV 0x2E,#0xFF` / `DJNZ 0x2E` at 0x0812 as
 * its delay loop. Bit 0x2E is the run-once guard meaning "the external chips
 * have been programmed"; byte 0x2E is a loop counter. Nothing but the opcode
 * distinguishes them.
 *
 * R5 SURVIVES A CALL. The EEPROM address low byte is loaded once at 0x04DE
 * (R5 = 0xFF) and used by the read at 0x04E2 AND by the write at 0x04EE, with
 * no reload in between -- Keil knew i2c_eeprom_read_byte leaves R5 alone. It
 * is reloaded at 0x04F1 before the second read because i2c_eeprom_write_byte
 * does clobber R5 (it uses it as a delay counter at 0x0BFD). This is the same
 * inter-procedural register analysis that forces std_get_interface into
 * cand/partial/; here it costs nothing because the function is assembly.
 *
 * ARGUMENT REGISTERS: cs8427_ctl_write takes register in R7 and value in R5;
 * i2c_eeprom_read_byte takes address high in R7, low in R5 and returns the
 * byte in R7; i2c_eeprom_write_byte takes high in R7, low in R5, data in R3.
 *
 * REV 22 CROSS-CHECK: cmd11_eeprom_selftest at rev22 0x04C8 is the same
 * function with relocated call targets -- same 0x1FFF address, same XRL
 * 0xFF complement, same CLR of bit 0x16 on success (rev22 0x04FE..0x0501),
 * same register 4 = 0x41 setup. Rev 22 adds one call (0x0FFA at 0x0506) and
 * reaches the register 0x12 = 0x00 write through the shared tail at 0x0509
 * rather than inline.
 *
 * NAKED: register parameters throughout, R5 held live across a call, and the
 * exit is the dispatcher switch's `break`. */
void cmd11_eeprom_selftest(void) __naked {
    __asm
        .globl _audio_path_reconfig_ext_chips
        .globl _audio_clock_mode_apply
        .globl _cs8427_ctl_write
        .globl _i2c_eeprom_read_byte
        .globl _i2c_eeprom_write_byte
        .globl _shiftreg8_commit

        jb    0x2e,00001$          ; BIT 0x2E = IRAM 0x25.6, ext chips done?
        lcall _audio_path_reconfig_ext_chips
    00001$:
        setb  0x2d                 ; BIT 0x2D = IRAM 0x25.5 = f_force
        mov   r7,#0x03             ; clock mode 3 = 48000 Hz
        lcall _audio_clock_mode_apply

        mov   0x2c,#0x04           ; BYTE 0x2C = CS8427 register 4
        mov   0x2d,#0x41           ; BYTE 0x2D = value 0x41
        mov   r5,0x2d
        mov   r7,0x2c
        lcall _cs8427_ctl_write

        ; --- the test proper: EEPROM[0x1FFF] ^= 0xFF, then read it back ---
        mov   r5,#0xff             ; address low  0xFF   (stays live to 0x04EE)
        mov   r7,#0x1f             ; address high 0x1F
        lcall _i2c_eeprom_read_byte
        mov   0x2c,r7              ; BYTE 0x2C = the byte that was there
        xrl   0x2c,#0xff           ;           = its ones-complement, the value
                                   ;             we expect to read back
        mov   r3,0x2c              ; data
        mov   r7,#0x1f             ; address high; R5 still 0xFF from above
        lcall _i2c_eeprom_write_byte
        mov   r5,#0xff             ; write_byte clobbered R5, reload it
        lcall _i2c_eeprom_read_byte   ; R7 still 0x1F -- write_byte leaves it
        mov   0x2d,r7
        mov   a,0x2d
        cjne  a,0x2c,00002$        ; read-back != expected -> leave the bit set
        clr   0x16                 ; BIT 0x16 = IRAM 0x22.6, cleared on PASS
    00002$:
        lcall _shiftreg8_commit    ; push chain A so the result is visible

        mov   0x2c,#0x12           ; BYTE 0x2C = CS8427 register 0x12
        clr   a
        mov   0x2d,a               ; BYTE 0x2D = value 0x00
        mov   r5,0x2d
        mov   r7,0x2c
        lcall _cs8427_ctl_write

        /* `sjmp _evt_dispatch_epilogue`, written self-relative because sdas
         * cannot encode a short jump to an external symbol. `.` is
         * area-relative, so the difference is the constant 0x55 at assembly
         * time and survives relocation: 0x050F + 2 + 0x53 = 0x0564. See
         * cmd12_set_cpt_mode1.c. */
        sjmp  . + (0x0564 - 0x050f)   ; -> evt_dispatch_epilogue
    __endasm;
}
