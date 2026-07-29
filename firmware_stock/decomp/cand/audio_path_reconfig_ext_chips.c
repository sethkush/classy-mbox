// MATCH: image=rev20 addr=0x080B len=155 func=audio_path_reconfig_ext_chips cflags=--peep-file,firmware_stock/decomp/keil.peep
/* One-shot bring-up of the analogue/S-PDIF path: latch the 16-bit control
 * chain into a known state, program the TAS1020B clock generators for the
 * 48 kHz family, then push ten control-register writes into the external
 * serial audio chip.
 *
 * RUN-ONCE, AND IT IS THE CALLERS THAT SAY SO. All four call sites are
 * guarded by the same two instructions:
 *
 *     JB   0x2E, skip        ; bit 0x2E = IRAM 0x25.6
 *     LCALL 0x080B
 *
 * at 0x035D/0x0360 (cmd1_apply_clock_mode), 0x038F/0x0392
 * (cmd2_apply_iface1_alt), 0x0416/0x0419 (cmd3_apply_iface2_alt) and
 * 0x04C4/0x04C7 (cmd11_eeprom_selftest). This function sets that same bit at
 * 0x0810 and never clears it, so the second and later calls are skipped by the
 * caller. Rev 22 is identical in shape: audio_hw_bringup at 0x09B6 sets bit
 * 0x2E at 0x09BB and its callers guard at 0x0366, 0x0396, 0x0419 and 0x04CB.
 * That is what pins IRAM 0x25.6 as "external chips already brought up", the
 * reading firmware_stock/disasm/PANEL_LEDS.md records as "hw-init-done".
 *
 * THE BIT/BYTE TRAP, IN ONE FUNCTION. `SETB 0x2E` at 0x0810 sets *bit* 0x2E,
 * which is IRAM 0x25 bit 6. `MOV 0x2E,#0xFF` at 0x0812 writes IRAM *byte*
 * 0x2E, an ordinary scratch location. They are unrelated storage and the two
 * print identically in a listing. The same pair occurs for 0x2F: `SETB 0x2F`
 * / `CLR 0x2F` (0x083E, 0x084B, 0x0850) drive IRAM 0x25 bit 7, the serial
 * chip-select line, while `MOV 0x2F,#0x10` (0x085B) stages a register value in
 * IRAM byte 0x2F.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY, and for a reason distinct from
 * hw_master_init's. The blocker here is not register allocation, it is that
 * Keil extracted four *common block subroutines* out of the ten
 * write(reg, val) calls at the bottom: 0x08A6, 0x08B3, 0x08BD and 0x08C4. Some
 * of the ten calls go through a helper and some are emitted inline, purely
 * according to which blocks the optimiser happened to group. SDCC has no such
 * transform, so no C source reproduces the call pattern, and the callee's
 * arguments are passed in R7/R5 (Keil's register-parameter convention) which
 * SDCC does not use either.
 *
 * The evidence that the source really was ten flat calls is Rev 22, which
 * compiles the same sequence with no helpers at all: audio_hw_bringup 0x09F8
 * onwards is ten literal `MOV R7,#reg / MOV R5,#val / LCALL 0x0C31` groups,
 * same registers, same values, same order. Rev 22 also spends R7 on the
 * settle delay (`MOV R7,#0xFF / DJNZ R7,$` at 0x09BD) where Rev 20 uses IRAM
 * byte 0x2E -- a register-allocation difference, not a source difference.
 *
 * THE 16-BIT CONTROL CHAIN. IRAM 0x23 and 0x25 are the two payload bytes
 * shifted out by shiftreg16_commit (0x0E62; rev22 shiftreg_out16_p1 at
 * 0x0E56). Bit numbering: bit B lives in IRAM 0x20+(B>>3) at bit B&7, so
 * 0x18..0x1F are IRAM 0x23.0..7 and 0x28..0x2F are IRAM 0x25.0..7. Of the bits
 * touched here, 0x25.6 (init-done) and 0x25.7 (serial chip select) are pinned;
 * 0x23.2, 0x23.3 and 0x23.4 are NOT identified. What is known about them is
 * only the timing: 0x23.2 and 0x23.3 are also cleared at the top of
 * audio_clock_mode_apply (0x072F/0x0731) and set again in its tail
 * (0x07EE/0x07F0), i.e. they are held low across any clock reprogramming and
 * released when the clock is stable. PANEL_LEDS.md notes this chain is a mixed
 * LED/control word and that the individual assignments are unresolved; nothing
 * here resolves them, so no function is claimed for those three bits.
 *
 * THE CHIP-SELECT PULSE at 0x084B..0x0854 -- CS driven low, latched, driven
 * high, latched -- is a bare pulse with no data. Every register write already
 * brackets itself with CS (cs8427_ctl_write clears bit 0x2F at 0x0C4F and sets
 * it at 0x0C8D), so this one is extra and precedes all ten writes. A control
 * port that has to be told which of its two protocol modes to use would be the
 * obvious explanation; that is an INFERENCE and is not verified here. No
 * CS8427 datasheet exists in this repo and rev20_ANNOTATED.md records the chip
 * identity itself as "likely", inferred from the constant 0x20 address byte at
 * 0x0C4B. Rev 22 emits the identical pulse at 0x09EE..0x09F7.
 *
 * THE TEN REGISTER WRITES, in order, exactly as the bytes give them:
 *
 *     reg 0x04 = 0x00      0x0855 -> helper 0x08A6
 *     reg 0x13 = 0x10      0x0858..0x0860
 *     reg 0x04 = 0x00      0x0861 -> helper 0x08A6
 *     reg 0x04 = 0x40      0x0864..0x086C
 *     reg 0x01 = 0x01      0x086D..0x0875
 *     reg 0x02 = 0x20      0x0876..0x087E
 *     reg 0x03 = 0x0C      0x087F..0x088B (inlined, not via a helper)
 *     reg 0x05 = 0x05      0x088C..0x0891 -> helper 0x08B3
 *     reg 0x06 = 0x05      0x0892..0x0897 -> helper 0x08B3
 *     reg 0x11 = 0xFF      0x0898..0x08A4 (inlined)
 *
 * Rev 22 writes the same ten pairs in the same order at 0x09F8..0x0A3D.
 * Register 4 being written 0x00 / (other reg) / 0x00 / 0x40 is the only
 * structure visible without a datasheet: something is parked at zero while
 * another register changes and then enabled. Register *numbers* and *values*
 * are certain; the register names are not established in this repo.
 */
void audio_path_reconfig_ext_chips(void) __naked {
    __asm
        .globl _shiftreg16_commit
        .globl _acg_48k_commit
        .globl _acg_incdptr_dctl_div2
        .globl _extchip_write_reg4_zero
        .globl _extchip_write_2e_2f
        .globl _extchip_write_2e_2f_dup
        .globl _extchip_write_val05
        .globl _cs8427_ctl_write

        ; ---- 0x080B: park the 16-bit control chain, claim the init-done bit --
        clr   a
        mov   0x25,a               ; chain high byte = 0
        mov   0x23,a               ; chain low  byte = 0
        setb  0x2e                 ; BIT 0x2E = IRAM 0x25.6: bring-up has run.
                                   ; The callers test this to skip the call.
        mov   0x2e,#0xff           ; BYTE 0x2E: settle counter. Unrelated storage.
    0001$:
        djnz  0x2e,0001$           ; ~255 iterations before the first latch
        lcall _shiftreg16_commit   ; 0x0E62: shifts IRAM 0x23 first (loaded at
                                   ; 0x0E64), then 0x25, out on P1

        ; ---- 0x081B: clock generators to the 48 kHz family -------------------
        lcall _acg_48k_commit      ; 0x0DEC: ACG1/ACG2 FRQ = 0x61A80F, ACGCTL = 0x06.
                                   ; Returns with DPTR left on ACGCTL (0xFFE1,
                                   ; loaded 0x0E10, stored 0x0E15).
        lcall _acg_incdptr_dctl_div2 ; 0x0E17: INC DPTR (0xFFE1 -> 0xFFE2), then
                                   ; ACG1DCTL = ACG2DCTL = 0x10. Entering one
                                   ; byte before 0x0E18 is how the caller reuses
                                   ; the DPTR the previous call left behind.
        mov   0x08,#0x03           ; g_clock_mode = 3 = 48 kHz. Same IRAM byte
                                   ; audio_clock_mode_apply writes (0x0791) and
                                   ; setup_get_sample_freq reads back.
        mov   dptr,#0xffe1         ; ACGCTL
        movx  a,@dptr
        orl   a,#0xc0              ; MCLKO1EN | MCLKO2EN: master clock outputs on
        movx  @dptr,a

        ; ---- 0x082B: release two control lines, one latch at a time ----------
        mov   0x2e,#0xff
    0002$:
        djnz  0x2e,0002$
        setb  0x1a                 ; IRAM 0x23.2 -- function not identified
        setb  0x1b                 ; IRAM 0x23.3 -- function not identified
        lcall _shiftreg16_commit

        mov   0x2e,#0xff
    0003$:
        djnz  0x2e,0003$
        setb  0x2f                 ; IRAM 0x25.7 = serial chip select, idle high
        setb  0x1c                 ; IRAM 0x23.4 -- function not identified
        lcall _shiftreg16_commit

        ; ---- 0x0845: a bare low pulse on chip select, no data ---------------
        mov   0x2e,#0xff
    0004$:
        djnz  0x2e,0004$
        clr   0x2f                 ; CS low
        lcall _shiftreg16_commit
        setb  0x2f                 ; CS high again
        lcall _shiftreg16_commit

        ; ---- 0x0855: ten control-register writes ----------------------------
        lcall _extchip_write_reg4_zero   ; reg 4 = 0x00
        mov   0x2e,#0x13
        mov   0x2f,#0x10
        lcall _extchip_write_2e_2f       ; reg 0x13 = 0x10
        lcall _extchip_write_reg4_zero   ; reg 4 = 0x00 again
        mov   0x2e,#0x04
        mov   0x2f,#0x40
        lcall _extchip_write_2e_2f       ; reg 4 = 0x40
        mov   0x2e,#0x01
        mov   0x2f,#0x01
        lcall _extchip_write_2e_2f_dup   ; reg 1 = 0x01  (the other identical copy)
        mov   0x2e,#0x02
        mov   0x2f,#0x20
        lcall _extchip_write_2e_2f_dup   ; reg 2 = 0x20
        mov   0x2e,#0x03
        mov   0x2f,#0x0c
        mov   r5,0x2f                    ; this one the optimiser left inline
        mov   r7,0x2e
        lcall _cs8427_ctl_write           ; reg 3 = 0x0C
        mov   0x2e,#0x05
        lcall _extchip_write_val05       ; reg 5 = 0x05
        mov   0x2e,#0x06
        lcall _extchip_write_val05       ; reg 6 = 0x05
        mov   0x2e,#0x11
        mov   0x2f,#0xff
        mov   r5,0x2f                    ; inline again -- last block, no pair
        mov   r7,0x2e
        lcall _cs8427_ctl_write           ; reg 0x11 = 0xFF
        ret
    __endasm;
}
