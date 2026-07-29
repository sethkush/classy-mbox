// MATCH: image=rev22 addr=0x0FFA len=7 func=stage_ctrl_pair_12_00 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Stage the argument pair for a cs8427_ctl_write: register 0x12, value 0x00.
 * IRAM 0x2C and 0x2D are that routine's staging bytes -- callers store here
 * and then reload R7 = reg, R5 = val before the call.
 *
 * THE 8051 BIT/BYTE TRAP IS LIVE HERE. `MOV 0x2C,#0x12` writes IRAM BYTE 0x2C.
 * BIT address 0x2C is IRAM 0x25 bit 4 -- f_spdif -- which the audio-path code
 * tests with JNB a few instructions earlier in the caller. Same two hex
 * digits, two different cells.
 *
 * `CLR A / MOV 0x2D,A` rather than `MOV 0x2D,#0` is Keil's encoding for a
 * store of zero; the keil.peep rule that turns SDCC's 3-byte form into it is
 * what makes this 7 bytes rather than 8.
 *
 * REV 20 -> REV 22 DELTA: the code exists in Rev 20 but not as a function.
 * There it is the second half of serial_ctl_write_04_41_then_12_00 (0x0568),
 * a single 26-byte block doing reg 0x04 <- 0x41 then reg 0x12 <- 0x00. Rev 22
 * split that block up: the 0x04/0x41 staging is elsewhere and this piece is
 * its own three-call-site helper (0x0488, 0x04AA, 0x0506) that only stages the
 * pair and returns, leaving the R5/R7 load and the call to the caller. Same
 * register writes reach the chip; the factoring moved. That the boundary moved
 * at all is the evidence that neither shape was a hand-written source function
 * -- both are Keil common-block extraction. */
__data __at (0x2C) unsigned char g_ctl_reg;   /* byte 0x2C, NOT bit 0x2C */
__data __at (0x2D) unsigned char g_ctl_val;
void stage_ctrl_pair_12_00(void) { g_ctl_reg = 0x12; g_ctl_val = 0; }
