// MATCH: image=rev22 addr=0x000A len=28 span=1 func=reti_stub_ie1 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Rev 22, 0x000A..0x0025: the upper 8051 hardware vectors plus the thirteen
 * one-byte VECINT no-op handlers packed into the gaps between them.
 *
 * Ghidra lists sixteen "functions" across this range (reti_stub_ie1,
 * tf0_vector, reti_stub_tf1, reti_stub_si, vecint_oep1_noop .. vecint_iep6_noop
 * and the three bare LJMPs it folds into the noop entries). They are one
 * 28-byte run, so this candidate claims all of it with span=1 -- the same
 * grouping the Rev 20 batch used in cand/vecint_noop_stubs.c, and for the same
 * reason: thirteen separate files each holding a single 0x22 byte would carry
 * no information that is not here.
 *
 * WHAT THE 0x22 BYTES ARE. Not padding. Every one is a live handler reached
 * through the TAS1020B VECINT dispatch table, which in Rev 22 is at 0x0C7D
 * (Rev 20: 0x0C93) -- 37 big-endian code addresses, one per VECINT value.
 * usb_isr_int0_vecdispatch (rev22 0x0DDF; rev20 0x0DAC) reads VECINT (XDATA
 * 0xFFB2), doubles it (ADD A,A) and adds the table base, so the entry index IS
 * the VECINT value. The handler address is loaded into R2:R1 and entered via
 * jmp_via_r2r1 (rev22 0x0BD4, rev20 0x0F96), which is an LCALL, so a handler
 * returns with RET into the ISR's VECINT acknowledge.
 *
 * That is why the RET/RETI split is the confirmation that this reading is
 * right: a stub reached from the dispatch table must end in RET (0x22); a body
 * reached by LJMP from a hardware vector must end in RETI (0x32) to clear the
 * core's in-service latch. The bytes obey it exactly -- 0x000A, 0x000E and
 * 0x000F are 0x32 and are the LJMP targets of the INT1, TIMER1 and UART
 * vectors; every byte reached from 0x0C7D is 0x22.
 *
 * The RETI stubs are dead code in the sense that the interrupts behind them are
 * never enabled -- hw_clock_codec_init does CLR ES (0xAC) at rev22 0x0810,
 * CLR EX1 (0xAA) at 0x0812, then SETB ET0 (0xA9) at 0x0814 and SETB EX0 (0xA8)
 * at 0x0818. So only INT0 and TIMER0 are armed, which is exactly the pair whose
 * vectors carry real LJMPs; INT1, TIMER1 and the UART get a bare RETI because
 * something has to be there.
 *
 * Each 8051 vector slot is 8 bytes and an LJMP costs 3, so 5 bytes are free per
 * slot. Keil's linker filled them with the unused-endpoint stubs. The stubs
 * that did not fit are at 0x1029..0x1035 (cand/rev22_vecint_noop_stubs_tail.c).
 *
 * VECINT names are TI's, from reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h
 * ("interrupt source"): OEP0_INT 0x00..OEP7_INT 0x07, IEP0_INT 0x08..IEP7_INT
 * 0x0F, STPOW_INT 0x10, SETUP_INT 0x12, PSOF_INT 0x13, SOF_INT 0x14, RESR_INT
 * 0x15, SUSR_INT 0x16, RSTR_INT 0x17, CPRX_INT 0x18, CPTX_INT 0x19, DPRX_INT
 * 0x1A, DPTX_INT 0x1B, I2CRX_INT 0x1C, I2CTX_INT 0x1D, XINT_INT 0x1F, NO_INT
 * 0x24. Index == VECINT value, so the index and the name are one fact; both are
 * given below. The mapping is corroborated by the entries that are NOT stubs:
 * entry 0 = OEP0_INT -> 0x0CC7 (the EP0-OUT handler), entry 8 = IEP0_INT ->
 * 0x0F91 (EP0-IN done), entry 18 = SETUP_INT -> 0x0026 (usb_setup_handler),
 * entry 22 = SUSR_INT -> 0x0006, entry 23 = RSTR_INT -> 0x0F64 (bus reset).
 *
 * REV 20 -> REV 22 DELTA for this block: byte-identical except the one LJMP
 * operand at 0x000C. Verified byte-for-byte over 0x000A..0x0025 against both
 * images:
 *
 *     0x000B TIMER0 vector   rev20 LJMP 0x101E   rev22 LJMP 0x1016
 *
 * Every 0x22 and 0x32 byte, and the three LJMPs at 0x0013 / 0x001B / 0x0023
 * (which target addresses inside this block and so did not move), are
 * identical. The TIMER0 target moved because timer0_tick_isr relocated with
 * the rest of the image, not because it changed.
 */
void reti_stub_ie1(void) __naked {
    __asm
        .globl _timer0_tick_isr

    0000$:
        reti                       ; 0x000A  INT1 body -- INT1 is disabled
                                   ;         (hw init clears EX1)
        ljmp  _timer0_tick_isr     ; 0x000B  TIMER0 vector -> rev22 0x1016,
                                   ;         the front-panel tick
    0001$:
        reti                       ; 0x000E  TIMER1 body -- TIMER1 disabled
    0002$:
        reti                       ; 0x000F  UART body -- serial disabled

        ;; leftover of the TIMER0 slot
        ret                        ; 0x0010  <- entry 1 @ 0x0C7F  OEP1_INT
        ret                        ; 0x0011  <- entry 2 @ 0x0C81  OEP2_INT
        ret                        ; 0x0012  <- entry 3 @ 0x0C83  OEP3_INT

        ljmp  0000$                ; 0x0013  INT1 vector -> the RETI at 0x000A

        ;; leftover of the INT1 slot. Entry 8 (IEP0_INT, @0x0C8D) points at
        ;; 0x0F91, a real handler, so these five bytes are contiguous in memory
        ;; but not contiguous entries in the table.
        ret                        ; 0x0016  <- entry 4 @ 0x0C85  OEP4_INT
        ret                        ; 0x0017  <- entry 5 @ 0x0C87  OEP5_INT
        ret                        ; 0x0018  <- entry 6 @ 0x0C89  OEP6_INT
        ret                        ; 0x0019  <- entry 7 @ 0x0C8B  OEP7_INT
        ret                        ; 0x001A  <- entry 9 @ 0x0C8F  IEP1_INT

        ljmp  0001$                ; 0x001B  TIMER1 vector -> the RETI at 0x000E

        ;; leftover of the TIMER1 slot
        ret                        ; 0x001E  <- entry 10 @ 0x0C91  IEP2_INT
        ret                        ; 0x001F  <- entry 11 @ 0x0C93  IEP3_INT
        ret                        ; 0x0020  <- entry 12 @ 0x0C95  IEP4_INT
        ret                        ; 0x0021  <- entry 13 @ 0x0C97  IEP5_INT
        ret                        ; 0x0022  <- entry 14 @ 0x0C99  IEP6_INT

        ljmp  0002$                ; 0x0023  UART vector -> the RETI at 0x000F
    __endasm;
}
