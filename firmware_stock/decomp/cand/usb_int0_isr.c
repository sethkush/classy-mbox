// MATCH: image=rev20 addr=0x0DAC len=63 func=usb_int0_isr cflags=--peep-file,firmware_stock/decomp/keil.peep

/* INT0 -- the USB engine's single interrupt line. Reached by LJMP from the
 * INT0 vector at 0x0003 (0x0003: 02 0d ac). Every USB event the TAS1020B can
 * report arrives here and is demultiplexed by reading VECINT (0xFFB2).
 *
 * VECINT INDEXING. Stock reads VECINT and then doubles it (`ADD A,ACC` at
 * 0x0DC0) before adding the table base 0x0C93. The table therefore has 2-byte
 * entries indexed by the VECINT value DIRECTLY, not by VECINT/2 -- worth
 * stating because the other common TI convention is a pre-scaled VECINT. The
 * table runs 0x0C93..0x0CDC, 37 entries, VECINT 0x00 through 0x24, and every
 * entry is a big-endian code address read out with two MOVC A,@A+DPTR.
 *
 * Table contents in Rev 20, by TI's VECINT constant. The constants are TI's,
 * from reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h ("interrupt source"
 * block: OEP0_INT 0x00..OEP7_INT 0x07, IEP0_INT 0x08..IEP7_INT 0x0F, STPOW_INT
 * 0x10, SETUP_INT 0x12, PSOF_INT 0x13, SOF_INT 0x14, RESR_INT 0x15, SUSR_INT
 * 0x16, RSTR_INT 0x17, CPRX_INT 0x18, CPTX_INT 0x19, DPRX_INT 0x1A, DPTX_INT
 * 0x1B, I2CRX_INT 0x1C, I2CTX_INT 0x1D, XINT_INT 0x1F, NO_INT 0x24), and the
 * numbering is confirmed by the five non-stub entries lining up with the
 * handlers they must be:
 *
 *   0x00 OEP0   -> 0x0D25   the EP0 OUT data-stage handler
 *   0x08 IEP0   -> 0x0FC4   usb_iep0_done_handler
 *   0x10 STPOW  -> 0x1032   RET stub (SETUP overwrite)
 *   0x12 SETUP  -> 0x0026   usb_ev_setup
 *   0x13 PSOF   -> 0x1033   RET stub
 *   0x14 SOF    -> 0x1034   RET stub
 *   0x15 RESR   -> 0x1035   RET stub
 *   0x16 SUSR   -> 0x0006   usb_ev_suspend (posts event 14)
 *   0x17 RSTR   -> 0x0F43   usb_rstr_handler
 *   0x18 CPRX   -> 0x1036   RET stub
 *   0x19 CPTX   -> 0x1037   RET stub
 *   0x24 NO_INT -> 0x103D   RET stub
 *
 * Every other entry points at a one-byte RET: either the RETs packed into the
 * unused interrupt-vector gaps (0x0010..0x0012, 0x0016..0x001A, 0x001E..0x0022)
 * or the block at 0x1031..0x103E. An unexpected vector is a no-op, not a crash.
 * Rev 20 has real handlers for exactly five vectors.
 *
 * Rev 22's table is at 0x0C7D and differs in one entry that matters: SOF (0x14)
 * points at 0x0D58 rather than a RET stub. That handler reads the DMA0 address
 * counters at 0xFFEB/0xFFEC, compares them against a saved copy, and can clear
 * DMACTL0 bit 7 (0xFFE8) -- a playback-DMA watchdog on the frame clock. Rev 20
 * runs nothing at all on SOF.
 *
 * THE INDIRECT CALL. The handler address is loaded into R2 (high) and R1 (low)
 * -- `MOV R2,0x16` at 0x0DD1 is `MOV R2,R6`, since PSW = 0x10 selected
 * register bank 2 and direct 0x16 IS R6 in that bank -- and then LCALLed
 * through the four-instruction trampoline at 0x0F96
 * (`MOV DPH,R2 / MOV DPL,R1 / CLR A / JMP @A+DPTR`). The trampoline JUMPs, so
 * the handler's own RET returns to 0x0DD9 here, inside the ISR. R3 = 0xFF is
 * the memory-type byte of Keil's generic-pointer register triple R3:R2:R1;
 * the trampoline never reads it. `MOV R7,A` at 0x0DBF is likewise dead.
 *
 * Rev 22 dropped both: its copy of this ISR at 0x0DDF is the same code minus
 * the `FF` at 0x0DBF and the `7B FF` at 0x0DD4, 60 bytes against 63, and its
 * trampoline moved to 0x0BD4. Those three bytes are the only difference
 * between the two ISRs.
 *
 * ACK. VECINT is re-addressed and written 0 at 0x0DD9..0x0DDD, after the
 * handler has run. INT0 is level-triggered here -- hw_master_init clears the
 * whole of TCON at rev20 0x08EB / rev22 0x080C (`MOV 0x88,A` with A == 0),
 * which puts IT0 = 0, and that is the only write to TCON as a byte in either
 * image. The other TCON writes are all `SETB 0x8C`, i.e. TR0 alone (main.c and
 * evt0e_usb_suspend_enter_and_resume.c), so IT0 is never set again and INT0
 * stays level-triggered for the life of the image -- so the ack is what
 * releases the line. Doing it after rather than before means an event arriving
 * while the
 * handler runs is not lost: it simply keeps INT0 asserted and the ISR fires
 * again on RETI.
 *
 * WRITTEN AS ASSEMBLY. Two independent reasons. The context save is
 * hand-chosen -- ACC/B/DPH/DPL/PSW pushed in that order and a switch to
 * register bank 2 instead of pushing R0-R7 -- and SDCC's `__interrupt`
 * generates its own frame and its own vector entry, neither of which can be
 * steered to this. And the dispatch is a computed MOVC pair feeding a
 * register-pair indirect call, which has no C spelling that survives SDCC's
 * code generation. */
void usb_int0_isr(void) __naked {
    __asm
        .globl _jmp_r2r1_trampoline

        push  acc
        push  b
        push  dph
        push  dpl
        push  psw
        mov   psw,#0x10            ; register bank 2 -- cheaper than 8 pushes
        clr   0xaf                 ; EA = 0

        mov   dptr,#0xffb2         ; VECINT
        movx  a,@dptr
        mov   r7,a                 ; dead; Rev 22 does not emit this byte
        add   a,acc                ; index * 2, entries are 2 bytes
        add   a,#0x93              ; table base 0x0C93, low
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
        mov   r3,#0xff             ; generic-pointer memory type; never read
        lcall _jmp_r2r1_trampoline ; JMPs there; the handler RETs back to us

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
