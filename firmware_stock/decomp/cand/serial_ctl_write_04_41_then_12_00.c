// MATCH: image=rev20 addr=0x0568 len=26 func=serial_ctl_write_04_41_then_12_00 cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Two back-to-back control writes to the external serial chip:
 *      reg 0x04 <- 0x41
 *      reg 0x12 <- 0x00
 *
 * IRAM 0x2C and 0x2D are the argument staging pair for cs8427_ctl_write
 * (0x0C45): the value is written to the byte, then reloaded into the register
 * the Keil convention wants (R7 = first parameter = reg, R5 = second = val).
 * Every caller of that routine in the image does the same two-step, which is
 * how the convention is established.
 *
 * THE 8051 BIT/BYTE TRAP IS LIVE IN THIS FUNCTION. `MOV 0x2C,#4` writes IRAM
 * byte 0x2C. Bit address 0x2C is IRAM 0x25 bit 4 -- f_spdif -- and the caller
 * three instructions earlier (0x0485, `JNB 0x2c,...`) tests exactly that bit.
 * Same two hex digits, two different cells, adjacent in the listing.
 *
 * Reached from the mode-2 (0x0488) and mode-3 (0x04A2) audio-path handlers,
 * on the S/PDIF branch of `JNB 0x2c` at 0x0485 / 0x049F; the analog branch
 * goes to serial_ctl_write_caller_pair_then_24_80 (0x0582) instead. Register
 * meanings are not established -- see cs8427_ctl_write for why the chip
 * identity is still open.
 *
 * The second call is a tail call, encoded LJMP rather than LCALL+RET, so this
 * function is declared __naked: SDCC would otherwise append its own RET after
 * the jump and overrun the stock length by one byte. The body is real C for
 * the two stores; only the register loads and the calls are assembly, because
 * R7/R5 parameter passing is not expressible.
 *
 * This is very likely another Keil common-block extraction rather than a
 * source function, on the same evidence as codec_port_cfg3_commit (0x0FF4):
 * rev 22 factors the same code differently. Its block at 0x0567 contains only
 * the FIRST write (reg 0x04 <- 0x41) and returns, and a separate three-
 * instruction block at 0x0575 holds just the reload-and-call. The boundary
 * moved because the surrounding code changed -- a hand-written function
 * boundary would not.
 */
__data __at (0x2C) unsigned char g_ctl_reg;   /* byte 0x2C, not bit 0x2C */
__data __at (0x2D) unsigned char g_ctl_val;

void serial_ctl_write_04_41_then_12_00(void) __naked {
    g_ctl_reg = 0x04;
    g_ctl_val = 0x41;
    __asm
        .globl _cs8427_ctl_write
        mov   r5,0x2d              ; R5 = val  (parameter 2)
        mov   r7,0x2c              ; R7 = reg  (parameter 1)
        lcall _cs8427_ctl_write
    __endasm;

    g_ctl_reg = 0x12;
    g_ctl_val = 0;                 /* CLR A / MOV dir,A -- Keil's encoding */
    __asm
        mov   r5,0x2d
        mov   r7,0x2c
        ljmp  _cs8427_ctl_write    ; tail call: no RET of our own
    __endasm;
}
