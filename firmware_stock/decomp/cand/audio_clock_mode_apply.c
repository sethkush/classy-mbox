// MATCH: image=rev20 addr=0x0728 len=227 func=audio_clock_mode_apply cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Apply an audio clock mode. Mode arrives in R7 -- Keil's register-parameter
 * convention, which SDCC does not share, so this is __naked regardless.
 *
 * Every rate change in the firmware funnels through here, and the mode is
 * stashed in IRAM 0x08 where the class GET_CUR handler reads it back. That is
 * what pins the numbering without guesswork:
 *
 *   1  idle, no sample clock   (ACGCTL = 0x0D)
 *   2  44100 Hz                (synth word 0x6A4B20)
 *   3  48000 Hz                (synth word 0x61A80F, via acg_48k_commit)
 *   4  no setup, straight to the common tail
 *   5  external / S/PDIF slaved: halves DIVB2 to 0x01
 *
 * The dispatch is a chain of ADD/DEC against the accumulator rather than
 * compares, which is why it reads oddly: A holds mode-2, then mode-3, then
 * mode-5, then mode-1, testing for zero at each step. */
void audio_clock_mode_apply(void) __naked {
    __asm
        .globl _shiftreg16_commit
        .globl _acg_set_both_dctl_10
        .globl _acg_commit_and_ctl
        .globl _acg_48k_commit
        .globl _queue_chip_reg4_val40
        .globl _sfr_store_then_acg_48k
        .globl _cs8427_ctl_write

        mov   0x2e,r7              ; requested mode
        clr   a
        mov   0x2f,a               ; settle-delay counter, high
        mov   0x30,a               ;                       low
        clr   0x1a
        clr   0x1b
        lcall _shiftreg16_commit
        mov   dptr,#0xffe2         ; ACG1DCTL -- passed to the helper in DPTR
        lcall _acg_set_both_dctl_10

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
        mov   0x31,#0x04           ; stage chip reg 4 = 0x41
        mov   0x32,#0x41
        ljmp  0009$

        ; ---- mode 2: 44100 Hz, synth word 0x6A4B20 into both ----
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
        mov   dptr,#0xfff9         ; ACG2FRQ0
        mov   a,#0x20
        lcall _acg_commit_and_ctl  ; stores the pending A, then ACGCTL = 0x06
        mov   0x08,#0x02
        lcall _queue_chip_reg4_val40
        ljmp  0009$

        ; ---- mode 3: 48000 Hz ----
    0003$:
        lcall _acg_48k_commit
        mov   0x08,#0x03
        lcall _queue_chip_reg4_val40
        sjmp  0009$

        ; ---- mode 5: external / S/PDIF slaved ----
    0005$:
        mov   dptr,#0xffb1         ; GLOBCTL
        movx  a,@dptr
        anl   a,#0xfe              ; CPTEN off -- codec regs are writable now
        movx  @dptr,a
        mov   dptr,#0xffd4         ; CPTRXCNF4
        mov   a,#0x01              ;   DIVB2 = /2, half the boot value
        movx  @dptr,a
        mov   dptr,#0xffb1         ; GLOBCTL
        movx  a,@dptr
        orl   a,#0x01              ; CPTEN back on
        lcall _sfr_store_then_acg_48k   ; commits the pending MOVX
        inc   dptr                 ; -> 0xFFB2 VECINT
        clr   a
        movx  @dptr,a
        mov   dptr,#0xfff6         ; ACG2DCTL
        mov   a,#0x10
        movx  @dptr,a
        setb  0x18
        setb  0x19
        lcall _shiftreg16_commit
        mov   0x08,#0x05
        lcall _queue_chip_reg4_val40

        ; ---- common tail ----
    0009$:
        mov   r5,0x32              ; staged chip value
        mov   r7,0x31              ; staged chip register
        lcall _cs8427_ctl_write
        mov   dptr,#0xffe1         ; ACGCTL
        movx  a,@dptr
        orl   a,#0xc0              ; MCLKO1EN | MCLKO2EN
        movx  @dptr,a
        mov   dptr,#0xff63         ; IEPBCTX1
        clr   a
        movx  @dptr,a
        mov   dptr,#0xff67         ; IEPBCTY1
        movx  @dptr,a
        mov   dptr,#0xff9b         ; OEPBCTX2
        movx  @dptr,a
        mov   dptr,#0xff9f         ; OEPBCTY2
        movx  @dptr,a
        mov   dptr,#0xff60         ; IEPCNF1
        mov   a,#0xc5              ;   enable | ISO | 6 bytes per sample
        movx  @dptr,a
        mov   dptr,#0xff98         ; OEPCNF2
        movx  @dptr,a
        setb  0x1a
        setb  0x1b
        lcall _shiftreg16_commit

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
