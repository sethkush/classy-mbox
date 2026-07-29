// MATCH: image=rev20 addr=0x000A len=28 span=1 func=int1_body_reti cflags=--peep-file,firmware_stock/decomp/keil.peep
/* The rest of the 8051 vector table, 0x000A..0x0025, plus thirteen one-byte
 * handlers packed into the gaps between the vectors.
 *
 * Ghidra lists twenty "functions" here. They are one 28-byte run and this
 * candidate claims all of it with span=1. Making twenty candidates, thirteen
 * of them a single 0x22 byte, would say nothing that this file does not.
 *
 * WHAT THE 0x22 BYTES ACTUALLY ARE. It is tempting to read a run of RET as
 * inter-vector padding. It is not padding: every one of them is a live
 * handler, reached through the USB-engine VECINT dispatch table at 0x0C93.
 * That table is 37 big-endian code addresses covering the TAS1020B VECINT
 * values, and the following entries point into this range (Ghidra records each
 * as an XREF, e.g. "XREF from CODE:0c95" on the byte at 0x0010):
 *
 *   0x0C95 -> 0x0010   0x0C97 -> 0x0011   0x0C99 -> 0x0012
 *   0x0C9B -> 0x0016   0x0C9D -> 0x0017   0x0C9F -> 0x0018   0x0CA1 -> 0x0019
 *   0x0CA5 -> 0x001A   0x0CA7 -> 0x001E   0x0CA9 -> 0x001F
 *   0x0CAB -> 0x0020   0x0CAD -> 0x0021   0x0CAF -> 0x0022
 *
 * So the vector gaps are being used as free real estate for the "this USB
 * endpoint interrupt is not used, do nothing" handlers. Each 8051 vector slot
 * is 8 bytes, an LJMP costs 3, and 5 bytes are left over; the INT1 and TIMER1
 * slots give five bytes each and the TIMER0 slot gives three, because its
 * first two leftover bytes are spent on the TIMER1 and UART RETI bodies. The
 * remaining stubs that did not fit are at 0x1031..0x103E
 * (cand/vecint_noop_stubs_tail.c).
 *
 * The RET/RETI split is the confirmation, and is the reason to trust the
 * reading. A stub reached by an LCALL out of the VECINT dispatcher must end in
 * RET (0x22); a body reached by an LJMP from a hardware vector must end in
 * RETI (0x32) to clear the core's in-service latch. The bytes follow that rule
 * exactly: 0x000A, 0x000E and 0x000F are 0x32 and are the targets of the INT1,
 * TIMER1 and UART vectors; every byte reached from 0x0C93 is 0x22.
 *
 * How the table is indexed is settled: usb_int0_isr reads VECINT (XDATA
 * 0xFFB2) at 0x0DBE, doubles it (ADD A,A at 0x0DC0) and adds 0x0C93, so the
 * entry index IS the VECINT value and the 37 entries cover VECINT 0..36. The
 * handler address is loaded into R2:R1 and reached via jmp_r2r1_trampoline
 * (0x0F96), and VECINT is cleared at 0x0DD9 to acknowledge.
 *
 * Which endpoint each VECINT value denotes is not guesswork either: TI's own
 * mapping is in this repo at
 * reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h ("interrupt source"),
 * which gives OEP0_INT 0x00 .. OEP7_INT 0x07, IEP0_INT 0x08 .. IEP7_INT 0x0F,
 * STPOW_INT 0x10, SETUP_INT 0x12, PSOF_INT 0x13, SOF_INT 0x14, RESR_INT 0x15,
 * SUSR_INT 0x16, RSTR_INT 0x17, CPRX_INT 0x18, CPTX_INT 0x19, and NO_INT 0x24.
 * Since the entry index IS the VECINT value, index and name are the same fact,
 * and both are given below. The mapping checks out against the entries that
 * are NOT stubs: entry 8 = IEP0_INT points at 0x0FC4, the EP0-IN done handler;
 * entry 18 = SETUP_INT at 0x0026; entry 22 = SUSR_INT at 0x0006, the suspend
 * event poster; entry 23 = RSTR_INT at 0x0F43, the bus-reset handler. Four
 * independent hits, so the offset is right.
 *
 * Both images are byte-identical over 0x0000..0x0025 apart from the three LJMP
 * targets: rev20 has RESET->0x0A09, INT0->0x0DAC, TIMER0->0x101E; rev22 has
 * RESET->0x092A, INT0->0x0DDF, TIMER0->0x1016. Every 0x22 and 0x32 byte, and
 * the LJMPs at 0x0013/0x001B/0x0023, are identical in both.
 */
void int1_body_reti(void) __naked {
    __asm
        .globl _timer0_isr_tick

    0000$:
        reti                       ; 0x000A  INT1 body -- INT1 is disabled
                                   ;         (CLR EX1, rev20 0x08EF)
        ljmp  _timer0_isr_tick     ; 0x000B  TIMER0 vector -> 0x101E, the panel
                                   ;         tick; TH0 reload 0xCE
    0001$:
        reti                       ; 0x000E  TIMER1 body -- TIMER1 disabled
    0002$:
        reti                       ; 0x000F  UART body -- serial disabled
                                   ;         (CLR ES, rev20 0x08EC)

        ;; leftover of the TIMER0 slot
        ret                        ; 0x0010  <- table entry 1 @ 0x0C95  OEP1_INT
        ret                        ; 0x0011  <- table entry 2 @ 0x0C97  OEP2_INT
        ret                        ; 0x0012  <- table entry 3 @ 0x0C99  OEP3_INT

        ljmp  0000$                ; 0x0013  INT1 vector -> the RETI at 0x000A

        ;; leftover of the INT1 slot. Note table entry 8 (IEP0_INT, @0x0CA3)
        ;; points at 0x0FC4, a real handler, so these five bytes are contiguous
        ;; in memory but not contiguous entries in the table.
        ret                        ; 0x0016  <- table entry 4 @ 0x0C9B  OEP4_INT
        ret                        ; 0x0017  <- table entry 5 @ 0x0C9D  OEP5_INT
        ret                        ; 0x0018  <- table entry 6 @ 0x0C9F  OEP6_INT
        ret                        ; 0x0019  <- table entry 7 @ 0x0CA1  OEP7_INT
        ret                        ; 0x001A  <- table entry 9 @ 0x0CA5  IEP1_INT

        ljmp  0001$                ; 0x001B  TIMER1 vector -> the RETI at 0x000E

        ;; leftover of the TIMER1 slot
        ret                        ; 0x001E  <- table entry 10 @ 0x0CA7  IEP2_INT
        ret                        ; 0x001F  <- table entry 11 @ 0x0CA9  IEP3_INT
        ret                        ; 0x0020  <- table entry 12 @ 0x0CAB  IEP4_INT
        ret                        ; 0x0021  <- table entry 13 @ 0x0CAD  IEP5_INT
        ret                        ; 0x0022  <- table entry 14 @ 0x0CAF  IEP6_INT

        ljmp  0002$                ; 0x0023  UART vector -> the RETI at 0x000F
    __endasm;
}
