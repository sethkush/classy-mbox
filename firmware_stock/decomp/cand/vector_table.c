// MATCH: image=rev20 addr=0x0000 len=6 span=1 func=reset_vector cflags=--peep-file,firmware_stock/decomp/keil.peep
/* The bottom two 8051 hardware vectors.
 *
 * Ghidra calls these two separate functions (reset_vector @ 0x0000,
 * int0_vector @ 0x0003). They are two entries of one vector table, so this
 * candidate claims both with span=1. The rest of the table is
 * cand/vecint_noop_stubs.c -- the split is only because usb_ev_suspend
 * occupies 0x0006..0x0009 between them and belongs to another candidate.
 *
 * Only two of the five 8051 vectors carry a real handler in this firmware:
 *
 *   0x0000 RESET  -> c51_startup      (0x0A09; rev22 0x092A)
 *   0x0003 INT0   -> usb_int0_isr     (0x0DAC; rev22 0x0DDF)
 *
 * INT0 is how the TAS1020B's USB engine interrupts the 8051 core: the engine
 * asserts the internal INT0 line and the ISR reads VECINT to find out which of
 * the ~37 USB/codec-port events fired. hw_master_init sets EX0 and clears EX1
 * and ES (SETB 0xA8 / CLR 0xAA / CLR 0xAC at rev20 0x08E9..0x08F2), which is
 * why INT1 and the UART vectors below only need a RETI.
 *
 * Written as assembly because a vector table is a placement of jumps at fixed
 * addresses, not a function; there is no C for it.
 */
void reset_vector(void) __naked {
    __asm
        .globl _c51_startup
        .globl _usb_int0_isr

        ljmp  _c51_startup         ; 0x0000 RESET
        ljmp  _usb_int0_isr        ; 0x0003 INT0 -- the USB engine
    __endasm;
}
