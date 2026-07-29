// MATCH: image=rev20 addr=0x0582 len=20 func=serial_ctl_write_caller_pair_then_24_80 cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Issue the control write the caller has already staged in IRAM 0x2C/0x2D,
 * then a second one: reg 0x24 <- 0x80.
 *
 * The first call takes its operands from the staging pair without writing
 * them, so the caller owns that half of the transaction:
 *      0x048E:  0x2C = 0x23, 0x2D = 0x00   -> reg 0x23 <- 0x00
 *      0x04A8:  0x2C = 0x23, 0x2D = 0x40   -> reg 0x23 <- 0x40
 * Both are the analog branch of `JNB 0x2c` (bit 0x2C = f_spdif = IRAM 0x25.4)
 * in the mode-2 and mode-3 audio-path handlers; the S/PDIF branch calls
 * serial_ctl_write_04_41_then_12_00 (0x0568) instead. So bit 0x40 of chip
 * register 0x23 is the one thing that differs between the two analog modes.
 * What that bit selects is not established here.
 *
 * Entering with half the argument pair already set is the tell that this is a
 * Keil common-block extraction, not a source-level function: a real function
 * would take the pair as parameters. Rev 22 cuts the same code at a different
 * point -- its block at 0x0575 is only `MOV R5,0x2D / MOV R7,0x2C / LCALL /
 * RET`, three instructions, with the 0x24 <- 0x80 write left inline in the
 * callers. The boundary moved between revisions because the surrounding code
 * did, which a hand-written boundary would not. Same reasoning as
 * codec_port_cfg3_commit (0x0FF4).
 *
 * __naked because the second call is a tail call encoded as LJMP; SDCC would
 * append a RET after it. R7 = reg, R5 = val is Keil's register parameter
 * convention for cs8427_ctl_write and cannot be expressed in C under SDCC.
 */
__data __at (0x2C) unsigned char g_ctl_reg;   /* byte 0x2C, not bit 0x2C */
__data __at (0x2D) unsigned char g_ctl_val;

void serial_ctl_write_caller_pair_then_24_80(void) __naked {
    __asm
        .globl _cs8427_ctl_write
        mov   r5,0x2d              ; R5 = val  (staged by the caller)
        mov   r7,0x2c              ; R7 = reg  (staged by the caller)
        lcall _cs8427_ctl_write
    __endasm;

    g_ctl_reg = 0x24;
    g_ctl_val = 0x80;
    __asm
        mov   r5,0x2d
        mov   r7,0x2c
        ljmp  _cs8427_ctl_write    ; tail call: no RET of our own
    __endasm;
}
