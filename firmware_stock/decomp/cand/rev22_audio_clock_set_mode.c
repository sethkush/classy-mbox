// MATCH: image=rev22 addr=0x070F len=221 func=audio_clock_set_mode cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Apply an audio clock mode. Rev 22 counterpart of rev20 audio_clock_mode_apply
 * (rev20 0x0728, 227 bytes; rev22 0x070F, 221 bytes).
 *
 * Mode arrives in R7 -- Keil's register-parameter convention, which SDCC does
 * not share, so this is __naked regardless, exactly as in Rev 20.
 *
 * The mode is stashed in IRAM 0x08, where the class GET_CUR handler reads it
 * back; that is what pins the numbering:
 *
 *   1  idle, no sample clock   (ACGCTL = 0x0D)
 *   2  44100 Hz                (synth word 0x6A4B20)
 *   3  48000 Hz                (synth word 0x61A80F)
 *   4  no setup, straight to the common tail
 *   5  external / S/PDIF slaved: CPTRXCNF4 DIVB2 halved to 0x01, then the
 *      48 kHz synth word and ACGCTL = 0x06 via the fall-through entry at
 *      0x0EC7, then ACG1DCTL = 0x00 with ACG2DCTL = 0x10.  Mode 5 remains the
 *      only path in either image that writes ACG1DCTL anything but 0x10, so it
 *      is the only one leaving the two dividers configured differently.
 *
 * The dispatch is still a chain of ADD/DEC against the accumulator rather than
 * compares: A holds mode-2, then mode-3, then mode-5, then mode-1, testing for
 * zero at each step.
 *
 * ---------------------------------------------------------------------------
 * REV 20 -> REV 22 DELTA.  Behaviour is unchanged; the six bytes saved are all
 * Keil re-drawing the boundaries of factored common code.  Verified by diffing
 * rev20[0x0728:0x0728+227] against rev22[0x070F:0x070F+221]; every differing
 * run is one of the four items below or a relocated call/jump operand.
 *
 * 1. queue_chip_reg4_val40 was a subroutine in Rev 20 (0x0E20, 7 bytes:
 *    `MOV 0x31,#4 / MOV 0x32,#0x40 / RET`) LCALLed from the ends of modes 2, 3
 *    and 5.  Rev 22 has no such subroutine: the two stores are inlined once at
 *    0x07A0, immediately before the common tail, and modes 2 and 3 jump there
 *    (LJMP 0x07A0 / SJMP 0x07A0) while mode 5 falls into it.  Cost: three
 *    LCALLs (9 B) replaced by 6 B inline, net -3 B here and -7 B of subroutine
 *    elsewhere in the image.  Mode 1 is unaffected -- it stages 0x41 rather
 *    than 0x40 and always wrote its own pair of MOVs.
 *
 * 2. The extracted-tail boundary in the ACG programming block moved one
 *    instruction EARLIER, which is what the other three bytes buy.  Rev 20's
 *    mode 2 loaded `MOV DPTR,#0xFFF9` (ACG2FRQ0) itself at 0x077D and then
 *    LCALLed 0x0E0F, a bare `MOVX @DPTR,A` that committed the A it was still
 *    holding.  Rev 22's mode 2 loads only `MOV A,#0x20` and LCALLs 0x0EE8,
 *    which loads DPTR = 0xFFF9 itself.  So the DPTR load now lives once inside
 *    the shared block instead of once per caller.  This is the same
 *    boundary-shift already recorded for codec_port_cfg3_commit (rev20 0x0FF4)
 *    -> cport_cnf3_write_enable (rev22 0x0FE2), where the shared block likewise
 *    absorbed its callers' `MOV DPTR,#0xFFDE`.  Two independent instances of
 *    the same effect is good evidence this is Keil's common-block extraction
 *    pass choosing a longer common suffix, not a source edit.
 *
 * 3. One pure codegen difference in the settle-delay loop, no size change:
 *    Rev 20's `JNZ` branched straight back to the loop top, then `INC 0x2F`
 *    high was followed by `SJMP` loop-top.  Rev 22's `JNZ` branches FORWARD to
 *    that same SJMP (rev22 0x07DF `JNZ 0x07E3`).  Identical semantics, one
 *    extra branch executed on the common path.  (The equivalent loop inside
 *    hw_clock_codec_init shows the same flip, in the same direction.)
 *
 * 4. Everything else is relocation: the helper addresses moved.
 *        helper                          rev20    rev22
 *        shiftreg16 commit               0x0E62   0x0E56  (shiftreg_out16_p1)
 *        ACG1/2DCTL = 0x10               0x0E18   0x0EF4
 *        ACG2FRQ0 + ACGCTL=6 tail        0x0E0F   0x0EE8  (boundary moved, #2)
 *        48 kHz synth word               0x0DEC   0x0EC8
 *        MOVX-then-48k fall-through      0x0DEB   0x0EC7
 *        3-wire serial write             0x0C45   0x0C31  (spi3wire_write_3bytes)
 *
 * No SFR address, no synthesizer word, no mode number and no IRAM location
 * changed.  Whatever Rev 22 fixed relative to Rev 20, it is not in here.
 */
void audio_clock_set_mode(void) __naked {
    __asm
        .globl _shiftreg_out16_p1
        .globl _acg_both_dctl_write_0x10
        .globl _acg2frq0_load_and_acgctl
        .globl _acg_both_synths_24576khz
        .globl _sfr_write_then_acg_program
        .globl _spi3wire_write_3bytes

        mov   0x2e,r7              ; requested mode
        clr   a
        mov   0x2f,a               ; settle-delay counter, high
        mov   0x30,a               ;                       low
        clr   0x1a
        clr   0x1b
        lcall _shiftreg_out16_p1
        mov   dptr,#0xffe2         ; ACG1DCTL -- passed to the helper in DPTR
        lcall _acg_both_dctl_write_0x10

        mov   a,0x2e
        add   a,#0xfe              ; A = mode - 2
        jz    0002$
        dec   a                    ; A = mode - 3
        jz    0003$
        add   a,#0xfe              ; A = mode - 5
        jz    0005$
        add   a,#0x04              ; A = mode - 1
        jnz   0009$                ; mode 4 or anything else: common tail

        ; ---- mode 1: idle, no sample clock ----
        mov   dptr,#0xffe1         ; ACGCTL
        mov   a,#0x0d
        movx  @dptr,a
        mov   0x08,#0x01
        mov   0x31,#0x04           ; stage CS8427 reg 4 = 0x41 (note: 0x41, the
        mov   0x32,#0x41           ;   odd one out; every other mode stages 0x40)
        ljmp  0009$

        ; ---- mode 2: 44100 Hz, synth word 0x6A4B20 into both synthesizers ----
    0002$:
        mov   dptr,#0xffe6         ; ACG1FRQ1
        mov   a,#0x4b
        movx  @dptr,a
        mov   dptr,#0xffe5         ; ACG1FRQ2
        mov   a,#0x6a
        movx  @dptr,a
        mov   dptr,#0xffe7         ; ACG1FRQ0
        mov   a,#0x20
        movx  @dptr,a
        mov   dptr,#0xfff8         ; ACG2FRQ1
        mov   a,#0x4b
        movx  @dptr,a
        mov   dptr,#0xfff7         ; ACG2FRQ2
        mov   a,#0x6a
        movx  @dptr,a
        mov   a,#0x20              ; ACG2FRQ0 value, handed over in A; the
        lcall _acg2frq0_load_and_acgctl   ; callee supplies DPTR = 0xFFF9 and
        mov   0x08,#0x02           ;   then commits ACGCTL = 0x06
        ljmp  0008$

        ; ---- mode 3: 48000 Hz ----
    0003$:
        lcall _acg_both_synths_24576khz  ; whole 0x61A80F word + ACGCTL = 0x06
        mov   0x08,#0x03
        sjmp  0008$

        ; ---- mode 5: external / S/PDIF slaved ----
    0005$:
        mov   dptr,#0xffb1         ; GLOBCTL
        movx  a,@dptr
        anl   a,#0xfe              ; CPTEN off -- codec port regs writable now
        movx  @dptr,a
        mov   dptr,#0xffd4         ; CPTRXCNF4
        mov   a,#0x01              ;   DIVB2 = /2, half the boot value of 0x03
        movx  @dptr,a
        mov   dptr,#0xffb1         ; GLOBCTL
        movx  a,@dptr
        orl   a,#0x01              ; CPTEN back on -- staged in A, not stored
        ; 0x0EC7 is a single MOVX @DPTR,A that commits that staged GLOBCTL
        ; value and then FALLS THROUGH into acg_both_synths_24576khz (0x0EC8)
        ; and on into acg2frq0_load_and_acgctl (0x0EE8).  So this one LCALL
        ; also reloads both synthesizers with the 48 kHz word and sets
        ; ACGCTL = 0x06, and returns with DPTR = 0xFFE1 (ACGCTL), NOT the
        ; 0xFFB1 it was called with.  The INC DPTR below depends on that.
        lcall _sfr_write_then_acg_program
        inc   dptr                 ; 0xFFE1 + 1 = 0xFFE2 ACG1DCTL
        clr   a
        movx  @dptr,a              ; ACG1DCTL = 0x00 -- the only write of
                                   ;   anything but 0x10 to this register in
                                   ;   either image (rev22 0x078D-0x078F)
        mov   dptr,#0xfff6         ; ACG2DCTL
        mov   a,#0x10
        movx  @dptr,a              ; ACG2DCTL = 0x10 as in every other mode:
                                   ;   ACG2 keeps dividing while ACG1's divider
                                   ;   is off, the shape an externally slaved
                                   ;   clock takes
        setb  0x18
        setb  0x19
        lcall _shiftreg_out16_p1
        mov   0x08,#0x05
        ; falls through into the staging block

        ; ---- staged CS8427 register write, inlined in Rev 22 ----
        ; Rev 20 reached this as LCALL queue_chip_reg4_val40 (0x0E20) from all
        ; three of modes 2, 3 and 5.
    0008$:
        mov   0x31,#0x04
        mov   0x32,#0x40

        ; ---- common tail (Ghidra splits it off as clockmode_commit_serial_write
        ;      at 0x07A6; both its XREFs are from inside this function, so it is
        ;      a label, not a function) ----
    0009$:
        mov   r5,0x32              ; staged chip value
        mov   r7,0x31              ; staged chip register
        lcall _spi3wire_write_3bytes
        mov   dptr,#0xffe1         ; ACGCTL
        movx  a,@dptr
        orl   a,#0xc0              ; MCLKO1EN | MCLKO2EN
        movx  @dptr,a
        mov   dptr,#0xff63         ; IEPDCNTX1
        clr   a
        movx  @dptr,a
        mov   dptr,#0xff67         ; IEPDCNTY1
        movx  @dptr,a
        mov   dptr,#0xff9b         ; OEPDCNTX2
        movx  @dptr,a
        mov   dptr,#0xff9f         ; OEPDCNTY2
        movx  @dptr,a              ; all four buffer byte-counts to 0: both
                                   ;   X/Y halves of IEP1 and OEP2 unarmed
        mov   dptr,#0xff60         ; IEPCNF1
        mov   a,#0xc5              ;   enable | ISO | 6 bytes per sample
        movx  @dptr,a
        mov   dptr,#0xff98         ; OEPCNF2
        movx  @dptr,a
        setb  0x1a
        setb  0x1b
        lcall _shiftreg_out16_p1

        ; settle delay: count 0x2F:0x30 up to 0x0F:0xFF
        clr   a
        mov   0x2f,a
        mov   0x30,a
    0010$:
        inc   0x30
        mov   a,0x30
        jnz   0011$
        inc   0x2f
    0011$:
        cjne  a,#0xff,0010$
        mov   a,0x2f
        cjne  a,#0x0f,0010$
        ret
    __endasm;
}
