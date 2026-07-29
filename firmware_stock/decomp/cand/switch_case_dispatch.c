// MATCH: image=rev20 addr=0x0F70 len=38 func=switch_case_dispatch cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Keil C51 runtime routine ?C?CCASE -- the searched-key switch helper.
 *
 * WRITTEN AS ASSEMBLY because it is not compiler output at all: it ships as
 * hand-written assembly in Keil's C51 library and it reads its own return
 * address off the stack, which C cannot express.
 *
 * Calling convention. The caller does `MOV A,key` / `LCALL ?C?CCASE` and then
 * places the case table inline immediately after the LCALL. The routine POPs
 * the return address into DPTR -- that is the table base -- and never returns
 * to it. Control leaves through `JMP @A+DPTR` with A = 0, i.e. an absolute
 * jump to the address stored in the matching entry.
 *
 * Table format:
 *     entry:  .db targetHi, .db targetLo, .db caseKey     (repeated)
 *     end:    .db 0, .db 0, .db defaultHi, .db defaultLo
 * The scan stops when BOTH address bytes of an entry are zero. That is a safe
 * sentinel because 0x0000 is the reset vector, so no case target can be there.
 * Note the address halves are stored big-endian, high byte first, which is
 * Keil's code-pointer order and the opposite of SDCC's.
 *
 * Register use: R0 holds the key for the whole scan, and is then reused to
 * stage the target's high byte because DPH cannot be written until after DPL
 * (writing DPH first would corrupt the table pointer still being read).
 *
 * Cost: linear in the number of cases, three code bytes per case plus this
 * 38-byte routine, against 3 bytes per code value for a dense table. Keil
 * picks this form when the case values are sparse. The only caller in rev20
 * is std_request_dispatch at 0x0118 (bRequest values 0,1,3,5..12: sparse
 * because USB 2.0 reserves 2 and 4).
 *
 * NOT PRESENT IN REV 22. Searching rev22_firmware_code.bin for the opening
 * `D0 83 D0 82` finds no occurrence, and rev22's standard-request dispatcher
 * at 0x010B is a dense `JMP @A+DPTR` table instead (range check `CJNE A,#0x0D`
 * at rev22 0x010F, table at rev22 0x011E, with the reserved codes 2 and 4
 * given explicit entries pointing at the default arm 0x02EF). So this is one
 * of the few places where the two images differ in construct and not just in
 * addresses.
 */
void switch_case_dispatch(void) __naked {
    __asm
        pop   dph                  ; return address = base of the inline table
        pop   dpl
        mov   r0,a                 ; R0 = the key being searched for

    0001$:                         ; --- examine the entry at DPTR ---
        clr   a
        movc  a,@a+dptr            ; entry[0] = target high byte
        jnz   0003$                ; non-zero -> a real case, go compare
        mov   a,#1
        movc  a,@a+dptr            ; entry[1] = target low byte
        jnz   0003$                ; still a real case (target 0x00xx)
        ;; both address bytes zero: terminator. The default target is the two
        ;; bytes that follow, so step over the sentinel and fall into the
        ;; common "load target from DPTR[0..1] and jump" tail below.
        inc   dptr
        inc   dptr

    0002$:                         ; --- take the target at DPTR[0..1] ---
        movc  a,@a+dptr            ; A is 0 here on both paths: cleared above
        mov   r0,a                 ; ... or left 0 by the XRL that matched
        mov   a,#1
        movc  a,@a+dptr
        mov   dpl,a                ; low half first -- DPH is still the table
        mov   dph,r0
        clr   a
        jmp   @a+dptr              ; tail jump; the caller frame is gone

    0003$:                         ; --- compare the key of this entry ---
        mov   a,#2
        movc  a,@a+dptr            ; entry[2] = case key
        xrl   a,r0                 ; zero iff it matches, and A==0 is exactly
        jz    0002$                ;   what the tail above needs
        inc   dptr                 ; no match: advance one 3-byte entry
        inc   dptr
        inc   dptr
        sjmp  0001$
    __endasm;
}
