// MATCH: image=rev22 addr=0x0567 len=14 func=cs8427_write_reg04_val41 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Stage register 0x04 = 0x41 in the argument pair and write it to the
 * external audio chip. Rev 22 at 0x0567, 14 bytes.
 *
 * IRAM BYTES 0x2C and 0x2D are the argument staging pair for
 * spi3wire_write_3bytes (0x0C31): each value is stored to the byte, then
 * reloaded into the register Keil's convention wants -- R7 = parameter 1 =
 * register index, R5 = parameter 2 = value.
 *
 * THE 8051 BIT/BYTE TRAP IS LIVE AT THIS EXACT ADDRESS. `MOV 0x2C,#4` writes
 * IRAM BYTE 0x2C. BIT address 0x2C is IRAM 0x25 bit 4 -- the S/PDIF-selected
 * flag -- and the caller three instructions earlier tests precisely that bit
 * (`JNB 0x2C,...` at rev22 0x0482 and 0x04A4). Two different cells, same two
 * hex digits, adjacent in the listing.
 *
 * Reached from the S/PDIF branch of both clock-mode handlers:
 * cmd7_set_clock_mode2_prog_spdif (call at 0x0485) and
 * cmd8_set_clock_mode3_prog_spdif (call at 0x04A7). The analogue branch goes
 * to cs8427_write_shadowed (0x0575) instead.
 *
 * ===================== REV 20 -> REV 22 DELTA ==========================
 *
 * THE FUNCTION BOUNDARY MOVED; THE WIRE SEQUENCE DID NOT. Rev 20's block at
 * 0x0568 (serial_ctl_write_04_41_then_12_00, 26 bytes) contained TWO writes,
 * reg 0x04 = 0x41 followed by reg 0x12 = 0x00, the second one as a tail-call
 * LJMP. Rev 22's block at 0x0567 contains ONLY the first write and returns
 * with a plain RET.
 *
 * The second write is still there; Rev 22 just spreads it over three blocks.
 * Traced and verified: the S/PDIF branch is `LCALL 0x0567 / LCALL 0x0FFA /
 * {LJMP,SJMP} 0x0509`, where 0x0FFA is `MOV 0x2C,#0x12 / CLR A / MOV 0x2D,A`
 * (stage_ctrl_pair_12_00) and 0x0509 is the shared write tail `MOV R5,0x2D /
 * MOV R7,0x2C / LCALL 0x0C31`. Net effect on the wire is reg 0x04 = 0x41 then
 * reg 0x12 = 0x00 -- exactly Rev 20's pair, in the same order. Nothing about
 * the chip programming changed.
 *
 * That a boundary moves between revisions is itself the evidence these blocks
 * were never source-level functions: they are Keil common-block extractions,
 * and the groups the optimiser found changed when the surrounding code did.
 *
 * The one call inside is pure relocation: rev20 0x0C45 -> rev22 0x0C31.
 *
 * __naked because the register loads and the call cannot be expressed in C
 * under SDCC (R7/R5 parameter passing is Keil's convention, not SDCC's).
 * The two stores are real C; only the tail is assembly.
 */
__data __at (0x2C) unsigned char g_ctl_reg;   /* BYTE 0x2C, not bit 0x2C */
__data __at (0x2D) unsigned char g_ctl_val;

void cs8427_write_reg04_val41(void) __naked {
    g_ctl_reg = 0x04;
    g_ctl_val = 0x41;
    __asm
        .globl _spi3wire_write_3bytes
        mov   r5,0x2d              ; R5 = val  (parameter 2)
        mov   r7,0x2c              ; R7 = reg  (parameter 1)
        lcall _spi3wire_write_3bytes ; 0x0C31: writes {0x20, 0x04, 0x41}
        ret
    __endasm;
}
