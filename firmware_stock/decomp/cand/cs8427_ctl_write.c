// MATCH: image=rev20 addr=0x0C45 len=78 func=cs8427_ctl_write cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Bit-banged 3-byte serial control write to the external chip on P1.4/P1.3.
 *
 *      cs8427_ctl_write(reg, val)   reg in R7, val in R5
 *
 * Keil's register calling convention puts the first char parameter in R7 and
 * the second in R5, which is why this cannot be plain C under SDCC (SDCC would
 * use DPL and a static spill slot). The wrappers at 0x0568 and 0x0582 stage
 * both operands through IRAM 0x2C/0x2D and reload R7/R5 immediately before the
 * call, which is how the convention is confirmed rather than assumed.
 *
 * Wire format, three bytes MSB first:
 *      0x20      fixed leading byte
 *      reg       register index
 *      val       value
 * on P1.4 = data, P1.3 = clock (pulsed high then low after each bit).
 *
 * Chip select is bit 0x2F -- IRAM 0x25 bit 7 -- which is *not* a port pin: it
 * is the top bit of g_panel_hi, the second payload byte of the 16-bit panel
 * shift-register chain. Clearing it (0x0C4F) and calling shiftreg16_commit
 * (0x0E62) drives the select line low through the chain; setting it and
 * calling again (0x0C8D/0x0C8F) releases it. So the transaction is framed by
 * two full 16-bit panel shifts, and every control write to this chip also
 * rewrites the panel latch outputs with their current values.
 *
 * Chip identity: NOT ESTABLISHED, and no CS8427 datasheet exists anywhere in
 * this repo -- reference/ holds TAS1020A/B material and Digidesign updater
 * artefacts only, nothing from Cirrus Logic. The name is Ghidra's; this
 * project rates the identification "likely, mechanics certain"
 * (firmware_stock/disasm/rev20_ANNOTATED.md:270) and nothing here settles it.
 * What is certain from the bytes is that byte 1 of every transaction is the
 * constant 0x20 (rev20 0x0C4B, rev22 0x0C35). If the part is a CS8427, that
 * constant lines up with its chip-address byte -- but that is a recollected
 * fact about a part whose datasheet is not here to check, so it is offered as
 * a reason the name was proposed, not as evidence for it. Cutting the other
 * way, two of the register indices the wrappers use, 0x23 (0x048E, 0x04A8) and
 * 0x24 (0x0589), are outside the 0x00-0x20 range a CS8427 is believed to
 * document, which I cannot reconcile. The mechanics below are byte-exact; the
 * part number is not a claim I am making, and neither is any register name
 * anywhere in this decompilation.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY, for two reasons that compound: the R7/R5
 * register parameters, and the same `_crol_(x,1)` intrinsic expansion that
 * makes shiftreg8_commit and shiftreg16_commit assembly -- a DJNZ countdown
 * around one RL A, operand staged through R7.
 *
 * Rev 22 has the same routine at 0x0C31, 76 bytes, with one allocation
 * difference: rev20 spills the reg parameter to IRAM 0x33 and moves val into
 * R1 (0x0C45-0x0C47), whereas rev22 leaves reg in R7 and only saves it to R1
 * (0x0C31 `MOV R1,0x07`), taking byte 2 from R1 and byte 3 from R5 directly.
 * Two bytes shorter, identical on the wire. Every P1 mask, the 0x20 lead byte
 * and the 0x2F chip select are unchanged (rev22 0x0C39 CLR 0x2F,
 * 0x0C77 SETB 0x2F, calls to its shiftreg16 at 0x0E56).
 *
 * IRAM 0x33 is also the last byte below the stack (SP starts at 0x33), so this
 * spill slot sits directly beneath the first pushed return address.
 */
void cs8427_ctl_write(void) __naked {
    __asm
        .globl _shiftreg16_commit

        mov   0x33,r7              ; save reg  (parameter 1)
        mov   r1,0x05              ; R1 <- R5: save val (parameter 2)
        mov   r4,#0x08             ; 8 bits left in the current byte
        mov   r3,#0x20             ; byte 1: the fixed chip-address byte
        mov   r2,#0x01             ; byte counter: 1 -> 2 -> 3
        clr   0x2f                 ; IRAM 0x25.7: chip select asserted low...
        lcall _shiftreg16_commit   ; ...and pushed out through the panel chain

    bitloop$:
        mov   a,r4
        jz    nextbyte$            ; this byte finished

        ; --- _crol_(r3, 1): old bit 7 rotates into bit 0 --------------------
        mov   r0,#0x01
        mov   r7,0x03              ; R7 <- R3 (the intrinsic's parameter reg)
        mov   a,r7
        inc   r0
        sjmp  rotest$
    rotate$:
        rl    a
    rotest$:
        djnz  r0,rotate$
        mov   r3,a

        ; --- present the bit on P1.4, clock it in on P1.3 -------------------
        jnb   0xe0,data0$          ; ACC.0
        orl   0x90,#0x10           ; P1.4 = 1
        sjmp  clock$
    data0$:
        anl   0x90,#0xef           ; P1.4 = 0
    clock$:
        orl   0x90,#0x08           ; P1.3 high
        anl   0x90,#0xf7           ; P1.3 low
        dec   r4
        sjmp  bitloop$

    nextbyte$:
        cjne  r2,#0x01,byte3$
        mov   r2,#0x02
        mov   r3,0x33              ; byte 2: the register index
        mov   r4,#0x08
        sjmp  bitloop$
    byte3$:
        cjne  r2,#0x02,done$
        mov   r2,#0x03
        mov   r3,0x01              ; byte 3: R1 = the value
        mov   r4,#0x08
        sjmp  bitloop$

    done$:
        setb  0x2f                 ; chip select released...
        lcall _shiftreg16_commit   ; ...and pushed out through the panel chain
        ret
    __endasm;
}
