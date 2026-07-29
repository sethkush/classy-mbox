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
 * WHAT 0x04 = 0x41 DOES. The chip is a Cirrus Logic CS8427 -- established, see
 * firmware_stock/decomp/FINDING_cs8427_confirmed.md -- and ALSA's CS8427
 * header names register 0x04 CS8427_REG_CLOCKSOURCE. 0x41 = 0100 0001:
 *
 *     bit 6    CS8427_RUN   = 1   clock on
 *     bits 5:4 CLKMASK      = 00  CS8427_CLK256, OMCK = 256*Fso
 *     bit 3    CS8427_OUTC  = 0   output time base = OMCK
 *     bit 2    CS8427_INC   = 0   input time base = recovered input clock
 *     bits 1:0 RXDMASK      = 01  CS8427_RXDAES3INPUT -- recover 256*Fsi
 *                                 FROM THE AES3 (S/PDIF) INPUT
 *
 * The bring-up default written by audio_hw_bringup (0x0A0D, `MOV R7,#0x4 /
 * MOV R5,#0x40`; rev20 0x0864..0x086C via extchip_write_2e_2f) is 0x40, whose
 * only difference is RXD = 00 = CS8427_RXDILRCK, recovering the clock from the
 * ILRCK pin instead. So this wrapper is the single byte that switches the
 * CS8427 from internally clocked to S/PDIF-input clocked, and it is written on
 * exactly the S/PDIF branch -- which is the clock-source half of the
 * "externally clocked / S/PDIF-slaved" reading of the S/PDIF modes.
 * (ALSA's header is a secondary source: cite it as "ALSA's CS8427 header names
 * this ...", not as datasheet text.)
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
