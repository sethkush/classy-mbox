// MATCH: image=rev22 addr=0x0000 len=6 span=1 func=reset_vector cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Rev 22, the bottom two 8051 hardware vectors.
 *
 * Ghidra calls these two functions (reset_vector @ 0x0000, int0_vector @
 * 0x0003). They are two entries of one vector table, so this candidate claims
 * both with span=1. The rest of the table is cand/rev22_vecint_noop_stubs.c;
 * the split exists only because usb_susr_handler occupies 0x0006..0x0009
 * between them and is its own candidate.
 *
 * Only two of the five 8051 vectors carry a real handler in this firmware:
 *
 *   0x0000 RESET  -> keil_c51_startup   (rev22 0x092A; rev20 0x0A09)
 *   0x0003 INT0   -> usb_isr_int0_vecdispatch (rev22 0x0DDF; rev20 0x0DAC)
 *
 * INT0 is how the TAS1020B's USB engine interrupts the 8051 core: the engine
 * asserts the internal INT0 line and the ISR reads VECINT (XDATA 0xFFB2) to
 * find out which of the 37 USB/codec-port events fired. hw_clock_codec_init
 * enables exactly this and the timer: CLR ES (bit 0xAC) at rev22 0x0810,
 * CLR EX1 (0xAA) at 0x0812, SETB ET0 (0xA9) at 0x0814, SETB EX0 (0xA8) at
 * 0x0818. That is why the INT1, TIMER1 and UART vectors in the block above
 * only need a RETI.
 *
 * Written as assembly because a vector table is a placement of jumps at fixed
 * addresses, not a function; there is no C for it.
 *
 * REV 20 -> REV 22 DELTA: same two instructions, both operands relocated.
 * No behavioural change.
 *
 *     0x0000  rev20 LJMP 0x0A09   rev22 LJMP 0x092A   C51 startup
 *     0x0003  rev20 LJMP 0x0DAC   rev22 LJMP 0x0DDF   USB INT0 ISR
 */
void reset_vector(void) __naked {
    __asm
        .globl _keil_c51_startup
        .globl _usb_isr_int0_vecdispatch

        ljmp  _keil_c51_startup            ; 0x0000 RESET
        ljmp  _usb_isr_int0_vecdispatch    ; 0x0003 INT0 -- the USB engine
    __endasm;
}
