// MATCH: image=rev20 addr=0x0F96 len=6 func=jmp_r2r1_trampoline cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Indirect-call trampoline: jump to the code address held in R2:R1.
 *
 * WRITTEN AS ASSEMBLY because setting DPTR from a register pair and then
 * transferring control through it is not expressible in C, and because this is
 * a Keil C51 runtime routine rather than compiler output. (It is the library's
 * indirect-call helper; the exact ?C? symbol name has not been verified here,
 * so it is not asserted.)
 *
 * It is entered with LCALL, so the return address of the *caller of the helper*
 * stays on the stack and the target's own RET goes back there -- the JMP acts
 * as a call. Nothing here needs saving: A is destroyed on purpose (it must be
 * zero for the indexed jump) and DPTR is the vehicle.
 *
 * Sole caller in rev20 is the USB interrupt service routine, at 0x0DD6. The
 * sequence there is worth recording because it is what gives every VECINT stub
 * in cand/vecint_noop_stubs.c its meaning:
 *
 *   0x0DB6  MOV PSW,#0x10        select register bank 2 (RS1=1), so "R6"
 *                                below is direct address 0x16
 *   0x0DBB  MOV DPTR,#0xFFB2 / MOVX A,@DPTR      read VECINT
 *   0x0DC0  ADD A,A              *2: the table holds 2-byte addresses
 *   0x0DC2  ADD A,#0x93 / ADDC A,#0x0C           + table base 0x0C93
 *   0x0DCC  MOVC A,@A+DPTR -> R6                 handler address, high byte
 *   0x0DCE  MOV A,#1 / MOVC -> R1                handler address, low byte
 *   0x0DD1  MOV R2,0x16          R2 = R6, i.e. the high byte
 *   0x0DD4  MOV R3,#0xFF         Keil generic-pointer memory type: code
 *   0x0DD6  LCALL 0x0F96         <- this routine
 *   0x0DD9  MOV DPTR,#0xFFB2 / CLR A / MOVX      acknowledge: VECINT = 0
 *
 * So the VECINT value is used directly as the index into the 37-entry table at
 * 0x0C93, and a handler returns with RET into 0x0DD9. That is why the unused
 * endpoint stubs are RET (0x22) and not RETI.
 *
 * Present in rev22 as well, byte-identical, at 0x0BD4 (Ghidra rev22 names it
 * jmp_via_r2r1); its caller there is 0x0E06 and its table is at 0x0C7D.
 */
void jmp_r2r1_trampoline(void) __naked {
    __asm
        mov   dph,r2
        mov   dpl,r1
        clr   a
        jmp   @a+dptr              ; tail jump: the RET of the target returns to our caller
    __endasm;
}
