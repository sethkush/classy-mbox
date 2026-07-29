// MATCH: image=rev22 addr=0x0BD4 len=6 func=jmp_via_r2r1 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Indirect-call trampoline: jump to the code address held in R2:R1.
 *
 * WRITTEN AS ASSEMBLY because loading DPTR from a register pair and then
 * transferring control through it is not expressible in C, and because this is
 * a Keil C51 runtime routine rather than compiler output -- the library's
 * indirect-call helper. (The exact ?C? symbol name has not been verified from
 * any artefact in this repo, so it is not asserted.)
 *
 * It is entered with LCALL, so the return address of the *caller of the helper*
 * stays on the stack and the target's own RET returns there -- the JMP acts as
 * a call. Nothing needs saving: A is deliberately destroyed (it must be zero
 * for the indexed jump) and DPTR is the vehicle.
 *
 * Sole caller in Rev 22 is the USB interrupt service routine, at 0x0E06. The
 * surrounding sequence is what gives every VECINT stub its meaning:
 *
 *   MOV DPTR,#0xFFB2 / MOVX A,@DPTR    read VECINT
 *   ADD A,A                            *2: the table holds 2-byte addresses
 *   ADD A,#0x7D / ADDC A,#0x0C         + table base 0x0C7D  (rev20: 0x0C93)
 *   MOVC A,@A+DPTR                     handler address, high byte
 *   MOV A,#1 / MOVC                    handler address, low byte
 *   R2 = high, R1 = low, R3 = 0xFF     Keil generic-pointer memory type: code
 *   LCALL 0x0BD4                       <- this routine
 *   MOV DPTR,#0xFFB2 / CLR A / MOVX    acknowledge: VECINT = 0
 *
 * So the VECINT value indexes the 37-entry table at 0x0C7D directly, and a
 * handler returns with RET into the acknowledge. That is why the unused
 * endpoint stubs are RET (0x22) and not RETI.
 *
 * REV 20 -> REV 22 DELTA: byte-identical (8a 83 89 82 e4 73), relocated from
 * rev20 0x0F96 to rev22 0x0BD4. The caller moved from rev20 0x0DD6 to rev22
 * 0x0E06 and the table base from 0x0C93 to 0x0C7D, but not one byte of this
 * routine changed. Ported from cand/jmp_r2r1_trampoline.c verbatim.
 */
void jmp_via_r2r1(void) __naked {
    __asm
        mov   dph,r2
        mov   dpl,r1
        clr   a
        jmp   @a+dptr              ; tail jump: the target's RET returns to our caller
    __endasm;
}
