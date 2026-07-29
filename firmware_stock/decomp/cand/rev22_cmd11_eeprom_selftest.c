// MATCH: image=rev22 addr=0x04C8 len=79 span=1 func=cmd11_eeprom_selftest cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 11, Rev 22: prove the EEPROM is writable and show the answer on the
 * panel. Counterpart of Rev 20's cmd11_eeprom_selftest at 0x04C4.
 *
 * WHAT IT DOES, IN ORDER
 *   1. If the external chips have not been brought up yet, bring them up
 *      (audio_hw_bringup, 0x09B6; Rev 20 called the same routine at 0x0810's
 *      container, audio_path_reconfig_ext_chips).
 *   2. Set bit 0x2D (IRAM 0x25.5, f_force), go to clock mode 3 (48 kHz), and
 *      put the CS8427 in the state the mode-1 path uses: register 4 = 0x41.
 *   3. Read EEPROM[0x1FFF], write back its ones-complement, read it again.
 *   4. If the read-back equals the complement, the write took: clear bit 0x16
 *      (IRAM 0x22.6) and shift chain A out, so the result is visible.
 *   5. Stage CS8427 register 0x12 = 0x00 and fall into the shared write tail.
 *
 * WHY THIS CANDIDATE IS 79 BYTES AND CARRIES span=1. Ghidra's function runs
 * 0x04C8..0x0516, and the last two things in that range are not part of the
 * self-test at all -- they are two merged tails that five other event handlers
 * jump into:
 *
 *   0x0509  MOV R5,0x2D / MOV R7,0x2C / LCALL spi3wire_write_3bytes
 *           / SJMP 0x0563          -- "write the staged register/value pair,
 *                                     then clear the pending event".
 *           Entered from cmd7 (0x048B LJMP, 0x049D SJMP) and cmd8 (0x04BE).
 *   0x0512  LCALL audio_clock_set_mode / SJMP 0x0563
 *                                  -- "apply the mode in R7, then clear the
 *                                     pending event".
 *           Entered from cmd4 (0x0466), cmd5 (0x0475), cmd6/12 (0x047A),
 *           cmd9 (0x04C2) and cmd10 (0x04C6).
 *
 * At source level these are the ends of sibling `case` bodies in the same
 * switch, and Keil merged them. They are claimed here because they physically
 * live inside this Ghidra function; they need symbols.map rows as entry
 * points (see the report / proposed/cmdrest.symbols).
 *
 * IRAM 0x22.6 IS NOT THIS FUNCTION'S BIT. It is bit 6 of the front-panel
 * chain-A latch byte; cand/shiftreg8_commit.c owns the account of it. Nothing
 * in either image reads it, and its nominal value is !(f_spdif | f_force).
 * This function sets f_force at 0x04CE, so by that rule the bit already
 * belongs clear; clearing it on PASS brings the latch into agreement, and a
 * FAIL leaves the latch disagreeing. "PASS indicator" describes that one
 * branch, not a meaning for the bit.
 *
 * THIS TEST IS DESTRUCTIVE AND IT DOES NOT RESTORE. EEPROM[0x1FFF] -- the last
 * byte of the 8 KB device -- is left holding the complement of whatever it
 * held before, and each invocation flips it again. It is inside the flashed
 * budget: the flasher's EEPROM_BUDGET is the whole 8192-byte part, so a
 * full-budget image is 18 header bytes plus payload 0x0012..0x1FFF. What makes
 * the flip harmless in this image is content, not extent -- Rev 22's last
 * non-0xFF code byte is at 0x1035 and everything above it to the end of the
 * payload is 0xFF erase-fill that nothing executes or reads. (Rev 20 is the
 * same shape, last non-0xFF byte at 0x103E.) An image that actually used its
 * full budget would have this handler corrupting its own last byte, and it is
 * in any case a real write to real EEPROM with real wear.
 *
 * BIT-VS-BYTE, TWICE, ON THE SAME PRINTED NUMBERS. `JB 0x2E` at 0x04C8 tests
 * BIT 0x2E = IRAM 0x25.6 (the "external chips already programmed" guard);
 * `SETB 0x2D` at 0x04CE sets BIT 0x2D = IRAM 0x25.5 (f_force). Ten bytes later
 * `MOV 0x2D,#0x41` writes IRAM BYTE 0x2D, a different location entirely -- the
 * value half of the register/value pair at bytes 0x2C/0x2D that the CS8427
 * helpers stage. Both spellings appear four instructions apart.
 *
 * R5 SURVIVES A CALL. The EEPROM address low byte is loaded once at 0x04E2
 * (R5 = 0xFF) and used by the read at 0x04E6 AND by the write at 0x04F2, with
 * no reload in between -- Keil knew i2c_eeprom_read_byte leaves R5 alone. It
 * is reloaded at 0x04F5 because i2c_eeprom_write3 does clobber R5. R7 is
 * likewise not reloaded before the second read: write3 leaves it holding 0x1F.
 * This is the inter-procedural register analysis that forces std_get_interface
 * into cand/partial/; here it costs nothing because the function is assembly.
 *
 * ARGUMENT REGISTERS: spi3wire_write_3bytes takes register in R7, value in R5;
 * i2c_eeprom_read_byte takes address high in R7, low in R5, returns the byte
 * in R7; i2c_eeprom_write3 takes high in R7, low in R5, data in R3.
 *
 * REV 20 -> REV 22 DELTA: BEHAVIOUR IDENTICAL, TAIL REFACTORED.
 * Byte for byte the test is the same: same 0x1FFF address (0x04E2/0x04E4 here,
 * rev20 0x04DE/0x04E0), same XRL #0xFF complement, same CJNE read-back
 * compare, same CLR of bit 0x16 on success, same register 4 = 0x41 setup, same
 * mode 3. Two structural differences, neither observable:
 *   - Rev 20 wrote register 0x12 = 0x00 inline (MOV 0x2C,#0x12 / CLR A /
 *     MOV 0x2D,A / MOV R5,0x2D / MOV R7,0x2C / LCALL) at 0x0500..0x050E.
 *     Rev 22 stages the pair by calling stage_ctrl_pair_12_00 (0x0FFA) and
 *     then falls into the shared write tail at 0x0509. Same two writes.
 *   - Rev 20's `SJMP evt_dispatch_epilogue` at 0x050F is reached from the
 *     inline write; Rev 22 reaches it from the tail at 0x0510.
 * Rev 20 was 77 bytes for the handler alone; Rev 22 is 65 bytes of handler
 * (0x04C8..0x0508) plus the 14 bytes of shared tail (0x0509..0x0516) counted
 * here.
 *
 * NAKED: register parameters throughout, R5 and R7 held live across calls, two
 * merged tails inside the body, and the exits are the dispatcher switch's
 * `break`. */
void cmd11_eeprom_selftest(void) __naked {
    __asm
        .globl _audio_hw_bringup
        .globl _audio_clock_set_mode
        .globl _spi3wire_write_3bytes
        .globl _i2c_eeprom_read_byte
        .globl _i2c_eeprom_write3
        .globl _shiftreg_out8_p1hi
        .globl _stage_ctrl_pair_12_00

        jb    0x2e,00001$          ; BIT 0x2E = IRAM 0x25.6, ext chips done?
        lcall _audio_hw_bringup    ; 0x09B6
    00001$:
        setb  0x2d                 ; BIT 0x2D = IRAM 0x25.5 = f_force
        mov   r7,#0x03             ; clock mode 3 = 48000 Hz
        lcall _audio_clock_set_mode

        mov   0x2c,#0x04           ; BYTE 0x2C = CS8427 register 4
        mov   0x2d,#0x41           ; BYTE 0x2D = value 0x41
        mov   r5,0x2d
        mov   r7,0x2c
        lcall _spi3wire_write_3bytes

        ; --- the test proper: EEPROM[0x1FFF] ^= 0xFF, then read it back ---
        mov   r5,#0xff             ; address low  0xFF  (stays live to 0x04F2)
        mov   r7,#0x1f             ; address high 0x1F  (stays live to 0x04F7)
        lcall _i2c_eeprom_read_byte
        mov   0x2c,r7              ; BYTE 0x2C = the byte that was there
        xrl   0x2c,#0xff           ;           = its ones-complement, the value
                                   ;             we expect to read back
        mov   r3,0x2c              ; data
        mov   r7,#0x1f             ; address high; R5 still 0xFF from above
        lcall _i2c_eeprom_write3
        mov   r5,#0xff             ; write3 clobbered R5, reload it
        lcall _i2c_eeprom_read_byte   ; R7 still 0x1F -- write3 leaves it
        mov   0x2d,r7
        mov   a,0x2d
        cjne  a,0x2c,00002$        ; read-back != expected -> leave the bit set
        clr   0x16                 ; BIT 0x16 = IRAM 0x22.6, cleared on PASS
    00002$:
        lcall _shiftreg_out8_p1hi  ; push chain A so the result is visible
        lcall _stage_ctrl_pair_12_00  ; BYTE 0x2C = 0x12, BYTE 0x2D = 0x00
                                      ; ...and fall into the write tail

        ;; ---- merged tail @ 0x0509 ----------------------------------------
        ;; "write the staged CS8427 register/value pair, then clear the event".
        ;; Also entered from cmd7 (0x048B, 0x049D) and cmd8 (0x04BE).
    00003$:
        mov   r5,0x2d              ; value
        mov   r7,0x2c              ; register number
        lcall _spi3wire_write_3bytes
        /* `sjmp _evt_dispatch_epilogue`, written self-relative because sdas
         * cannot encode a short jump to an external symbol. `.` is
         * area-relative, so the difference is the constant 0x51 at assembly
         * time and survives relocation: 0x0510 + 2 + 0x51 = 0x0563. */
        sjmp  . + (0x0563 - 0x0510)   ; -> evt_dispatch_epilogue

        ;; ---- merged tail @ 0x0512 ----------------------------------------
        ;; "apply the clock mode already in R7, then clear the event".
        ;; Entered from cmd4 (0x0466), cmd5 (0x0475), cmd6/12 (0x047A),
        ;; cmd9 (0x04C2) and cmd10 (0x04C6). Never reached by fall-through --
        ;; the tail above always jumps away.
    00004$:
        lcall _audio_clock_set_mode
        sjmp  . + (0x0563 - 0x0515)   ; -> evt_dispatch_epilogue
    __endasm;
}
