// MATCH: image=rev22 addr=0x0DDF len=60 func=usb_isr_int0_vecdispatch cflags=--peep-file,firmware_stock/decomp/keil.peep

/* INT0 -- the USB engine's single interrupt line. Rev 22 at 0x0DDF, reached by
 * LJMP from the INT0 vector at 0x0003 (rev22 0x0003: 02 0d df; rev20 0x0003:
 * 02 0d ac). Every USB event the TAS1020B can report arrives here and is
 * demultiplexed by reading VECINT (0xFFB2).
 *
 * ================= REV 20 -> REV 22 DELTA =================
 * Three bytes deleted, one operand changed, one constant changed. No control
 * flow changed. rev20 0x0DAC is 63 bytes, rev22 0x0DDF is 60:
 *
 *   1. rev20 0x0DBF `FF`     MOV R7,A  after reading VECINT -- deleted.
 *      It was dead in Rev 20 too: nothing ever read R7 again before the
 *      indirect call.
 *   2. rev20 0x0DD4 `7B FF`  MOV R3,#0xFF -- deleted. R3 is the memory-type
 *      byte of Keil's generic-pointer register triple R3:R2:R1; the trampoline
 *      only reads R2 and R1, so this was dead as well.
 *   3. table base immediate  rev20 `24 93` (0x0C93) -> rev22 `24 7D` (0x0C7D).
 *   4. trampoline call       rev20 LCALL 0x0F96 -> rev22 LCALL 0x0BD4.
 *
 * Deleting two provably dead instructions and relocating two operands is the
 * whole of it. The dispatcher itself is unchanged; the Rev 22 fix lives
 * entirely in one table entry (below) and in the handler that entry points at.
 * The Rev 20 candidate cand/usb_int0_isr.c ported with those five bytes edited.
 *
 * ================= VECINT INDEXING =================
 * Stock reads VECINT and DOUBLES it (`ADD A,ACC` at rev22 0x0DF2, rev20
 * 0x0DC0) before adding the table base. The table therefore has 2-byte entries
 * indexed by the VECINT value DIRECTLY, not by VECINT/2 -- worth stating
 * because the other common TI convention is a pre-scaled VECINT. It runs
 * 0x0C7D..0x0CC6, 37 entries, VECINT 0x00 through 0x24, each a big-endian code
 * address read out with two MOVC A,@A+DPTR.
 *
 * ================= THE TABLE, AND THE ONE ENTRY THAT MATTERS =================
 * VECINT constants are TI's, from
 * reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h ("interrupt source" block:
 * OEP0_INT 0x00..OEP7_INT 0x07, IEP0_INT 0x08..IEP7_INT 0x0F, STPOW_INT 0x10,
 * SETUP_INT 0x12, PSOF_INT 0x13, SOF_INT 0x14, RESR_INT 0x15, SUSR_INT 0x16,
 * RSTR_INT 0x17, CPRX_INT 0x18, CPTX_INT 0x19, DPRX_INT 0x1A, DPTX_INT 0x1B,
 * I2CRX_INT 0x1C, I2CTX_INT 0x1D, XINT_INT 0x1F, NO_INT 0x24).
 *
 * Every non-stub entry, read out of rev22_firmware_code.bin at 0x0C7D:
 *
 *     VECINT              rev20 -> handler   rev22 -> handler
 *     0x00 OEP0            0x0D25             0x0CC7   EP0 OUT data stage
 *     0x08 IEP0            0x0FC4             0x0F91   EP0 IN completed
 *     0x12 SETUP           0x0026             0x0026   SETUP packet arrived
 *     0x14 SOF             0x1034 (RET stub)  0x0D58   <-- THE REV 22 FIX
 *     0x16 SUSR            0x0006             0x0006   posts event 14
 *     0x17 RSTR            0x0F43             0x0F64   bus reset
 *
 * Rev 20 has real handlers for five vectors; Rev 22 has six, and the sixth is
 * SOF. Everything else in the table points at a one-byte RET -- either the
 * RETs packed into unused interrupt-vector gaps (0x0010..0x0012,
 * 0x0016..0x001A, 0x001E..0x0022, 0x0026 excepted) or the block at
 * 0x1029..0x1035. An unexpected vector is a no-op, not a crash.
 *
 * SOF WAS NEVER MASKED OFF. USBIMSK is written 0x9F by both images (rev22
 * usb_ep_dma_init 0x0917, rev22 usb_rstr_handler 0x0F8D; rev20 0x09EC and
 * 0x0F53), and bit 4 of 0x9F is the SOF interrupt enable. So Rev 20 was
 * already taking a SOF interrupt every millisecond, running this dispatcher,
 * and landing on a RET. The entire cost of the Rev 22 fix is the two bytes at
 * 0x0CA5 plus 70 bytes of handler.
 *
 * ================= THE INDIRECT CALL =================
 * The handler address is loaded into R2 (high) and R1 (low) -- `MOV R2,0x16`
 * at rev22 0x0E03 is `MOV R2,R6`, since PSW = 0x10 selected register bank 2
 * and direct 0x16 IS R6 in that bank -- and then LCALLed through the
 * four-instruction trampoline at rev22 0x0BD4 (rev20 0x0F96):
 * `MOV DPH,R2 / MOV DPL,R1 / CLR A / JMP @A+DPTR`. The trampoline JUMPs, so
 * the handler's own RET returns to 0x0E09 here, inside the ISR. That is why
 * every entry in the table ends in RET and not RETI, and why the stubs can be
 * single RET bytes scavenged from the interrupt-vector gaps.
 *
 * ================= ACK ORDERING =================
 * VECINT is re-addressed and written 0 at 0x0E09..0x0E0D, AFTER the handler
 * has run. INT0 is level-triggered here -- hw_master_init clears the whole of
 * TCON at rev22 0x080C / rev20 0x08EB (`MOV 0x88,A` with A == 0), which puts
 * IT0 = 0, and that is the only write to TCON as a byte in either image; the
 * other TCON writes are all `SETB 0x8C`, i.e. TR0 alone. So IT0 is never set
 * again and INT0 stays level-triggered for the life of the image, which makes
 * the VECINT write the thing that releases the line. Acking after rather than
 * before means an event arriving while the handler runs is not lost: it keeps
 * INT0 asserted and the ISR fires again on RETI.
 *
 * That matters more in Rev 22 than in Rev 20, because sof_int_handler is the
 * first handler in either image that can take a long time -- it calls the
 * 16-bit divide routine at 0x0B7F, whose worst case is an 8-iteration
 * shift-subtract loop. Interrupts stay off for the whole of it (CLR EA at
 * 0x0DEC, SETB EA at 0x0E0E), so a SETUP packet arriving mid-divide is
 * deferred, not dropped.
 *
 * ================= WRITTEN AS ASSEMBLY =================
 * Two independent reasons. The context save is hand-chosen -- ACC/B/DPH/DPL/PSW
 * pushed in that order and a switch to register bank 2 instead of pushing
 * R0-R7 -- and SDCC's `__interrupt` generates its own frame and its own vector
 * entry, neither of which can be steered to this. And the dispatch is a
 * computed MOVC pair feeding a register-pair indirect call, which has no C
 * spelling that survives SDCC's code generation. */
void usb_isr_int0_vecdispatch(void) __naked {
    __asm
        .globl _jmp_via_r2r1

        push  acc
        push  b
        push  dph
        push  dpl
        push  psw
        mov   psw,#0x10            ; register bank 2 -- cheaper than 8 pushes
        clr   0xaf                 ; EA = 0

        mov   dptr,#0xffb2         ; VECINT
        movx  a,@dptr
                                   ; rev20 had a dead MOV R7,A here; rev22 does not
        add   a,acc                ; index * 2, entries are 2 bytes
        add   a,#0x7d              ; table base 0x0C7D, low  (rev20: 0x93)
        mov   dpl,a
        clr   a
        addc  a,#0x0c              ; ... and high, with the carry
        mov   dph,a

        clr   a
        movc  a,@a+dptr            ; handler address, high byte
        mov   r6,a
        mov   a,#0x01
        movc  a,@a+dptr            ; handler address, low byte
        mov   r2,0x16              ; = MOV R2,R6  (bank 2: direct 0x16 is R6)
        mov   r1,a
                                   ; rev20 had MOV R3,#0xFF here; rev22 does not
        lcall _jmp_via_r2r1        ; JMPs there; the handler RETs back to us

        mov   dptr,#0xffb2         ; VECINT
        clr   a
        movx  @dptr,a              ; ack, after the handler, not before

        setb  0xaf                 ; EA = 1
        pop   psw
        pop   dpl
        pop   dph
        pop   b
        pop   acc
        reti
    __endasm;
}
