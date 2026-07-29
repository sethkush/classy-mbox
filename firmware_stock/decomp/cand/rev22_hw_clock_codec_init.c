// MATCH: image=rev22 addr=0x07EC len=165 func=hw_clock_codec_init cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Master hardware bring-up: timers, interrupt enables, codec port, clock
 * generators, front-panel shift registers.  Run once from main() before USB is
 * attached.  Rev 22 counterpart of rev20 hw_master_init (rev20 0x08CB).
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY, for the same reason as the Rev 20 version:
 * Keil allocated the accumulator across the whole function -- A is loaded with
 * 0 once and stays live through fifteen instructions, `INC A` produces the 1
 * for MEMCFG, and DPTR is carried from 0xFFB0 to 0xFFB1 by a 1-byte INC DPTR
 * across fifteen unrelated SFR writes.  That is global register allocation,
 * not a peephole-sized difference.
 *
 * Ordering that matters:
 *   - USBCTL is cleared FIRST, so the host cannot enumerate the device while
 *     the codec port is half-configured.
 *   - GLOBCTL.CPTEN is set LAST, after every CPTCNF/CPTRXCNF register, because
 *     those registers are only writable while the codec port is disabled.
 *
 * ---------------------------------------------------------------------------
 * REV 20 -> REV 22 DELTA.  Both are 165 bytes.  Diffing
 * rev20[0x08CB:+165] against rev22[0x07EC:+165] gives exactly nine differing
 * bytes, and eight of them are relocated call operands:
 *
 *     offset  what                                   rev20    rev22
 *     0x64-65 LCALL MOVX-then-48k-synth entry        0x0DEB   0x0EC7
 *     0x68    LCALL INC-DPTR-then-both-DCTL entry    0x0E17   0x0EF3
 *     0x79-7A LCALL 8-bit shift register commit      0x0F0C   0x0EFC
 *     0x9A-9B LCALL 8-bit shift register commit      0x0F0C   0x0EFC
 *     0xA3    LCALL 16-bit shift register commit     0x0E62   0x0E56
 *
 * The ninth, offset 0x8B, is a branch displacement and the only codegen
 * difference: in the settle-delay loop Rev 20's `JNZ` (rev20 0x0955) branches
 * straight back to the loop top at 0x0946, whereas Rev 22's (rev22 0x0876)
 * branches FORWARD two bytes to the `SJMP` that goes to the loop top.  Same
 * instruction count, same size, identical semantics -- one extra taken branch
 * per iteration.  The same flip appears in audio_clock_set_mode's copy of this
 * loop, so it is a compiler-run difference, not a source change.
 *
 * Every SFR address, every constant written to one, the timer reloads, the
 * interrupt-enable pattern, the codec-port configuration and the settle-delay
 * bound are IDENTICAL between the two revisions.  Nothing Rev 22 fixed is in
 * this function.
 */
void hw_clock_codec_init(void) __naked {
    __asm
        .globl _sfr_write_then_acg_program
        .globl _acg_dividers_div2
        .globl _shiftreg_out8_p1hi
        .globl _shiftreg_out16_p1

        clr   a                    ; A = 0, and it stays 0 for the next 15 insns
        mov   0x2e,a               ; settle-delay counter, high
        mov   0x2f,a               ; settle-delay counter, low

        mov   dptr,#0xfffc         ; USBCTL
        movx  @dptr,a              ; detach: clear CONN before anything else
        mov   dptr,#0xffb0         ; MEMCFG
        inc   a                    ; A = 1  (cheaper than MOV A,#1)
        movx  @dptr,a              ; SDW: code fetches come from shadow RAM

        clr   a
        mov   0x90,a               ; P1  = 0     both shift-register chains idle
        mov   0xb0,#0xff           ; P3  = 0xFF  button inputs, pulled high
        mov   0x8c,#0xce           ; TH0 = 0xCE  panel tick reload
        mov   0x8a,a               ; TL0 = 0
        mov   0x8d,a               ; TH1 = 0
        mov   0x8b,a               ; TL1 = 0
        mov   0x89,#0x11           ; TMOD both timers 16-bit mode 1
        mov   0x88,a               ; TCON = 0

        clr   0xaf                 ; EA  = 0   interrupts off during setup
        clr   0xac                 ; ES  = 0
        clr   0xaa                 ; EX1 = 0
        setb  0xa9                 ; ET0 = 1   panel tick
        clr   0xab                 ; ET1 = 0
        setb  0xa8                 ; EX0 = 1   USB engine
        mov   0xb8,a               ; IP  = 0   no priority overrides

        inc   dptr                 ; DPTR 0xFFB0 -> 0xFFB1 (GLOBCTL), still live
        mov   a,#0x06
        movx  @dptr,a              ; 12 MHz, ext int off, LPWR on, CODEC OFF

        mov   dptr,#0xffe0         ; CPTCNF1
        mov   a,#0x0d              ;   2 time slots, I2S mode 5
        movx  @dptr,a
        mov   dptr,#0xffdf         ; CPTCNF2
        mov   a,#0xe5
        movx  @dptr,a
        mov   dptr,#0xffde         ; CPTCNF3
        mov   a,#0xac              ;   BYOR set: big-endian on the wire
        movx  @dptr,a
        mov   dptr,#0xffdd         ; CPTCNF4
        mov   a,#0x03              ;   DIVB = /4
        movx  @dptr,a
        mov   dptr,#0xffdc         ; CPTCTL
        mov   a,#0x50
        movx  @dptr,a
        mov   dptr,#0xffd6         ; CPTRXCNF2
        mov   a,#0x25
        movx  @dptr,a
        mov   dptr,#0xffd5         ; CPTRXCNF3
        mov   a,#0xac
        movx  @dptr,a
        mov   dptr,#0xffd4         ; CPTRXCNF4
        mov   a,#0x03              ;   DIVB2 = /4; the mode-5 branch of
                                   ;   audio_clock_set_mode rewrites this to 0x01

        lcall _sfr_write_then_acg_program ; commits the pending MOVX above, then
                                          ; falls through into the 48 kHz synth
                                          ; word for both ACGs and ACGCTL = 0x06
        lcall _acg_dividers_div2          ; ACG1DCTL = ACG2DCTL = 0x10  (/2);
                                          ; entered one byte early so the INC
                                          ; DPTR steps 0xFFE1 -> 0xFFE2

        mov   dptr,#0xffb1         ; GLOBCTL
        movx  a,@dptr
        orl   a,#0x01              ; CPTEN, only now that the codec port is set
        movx  @dptr,a
        mov   0x08,#0x03           ; current clock mode = 3 = 48 kHz

        clr   a
        mov   0x22,a               ; chain A payload = 0
        setb  0x1e                 ; IRAM 0x23.6 high while settling
        lcall _shiftreg_out8_p1hi

        ; Settle delay: count 0x2E:0x2F up to 0x0F:0xFF, about 4000 iterations.
        ; The exit test is "low == 0xFF && high == 0x0F", with CPL A standing
        ; in for the comparison against 0xFF.
    0001$:
        mov   a,0x2f
        cpl   a                    ; zero iff low byte is 0xFF
        jnz   0002$                ; low not 0xFF -> A non-zero -> keep going
        mov   a,0x2e
        xrl   a,#0x0f              ; zero iff high byte is 0x0F
    0002$:
        jz    0003$                ; both matched -> done
        inc   0x2f
        mov   a,0x2f
        jnz   0004$                ; <-- Rev 22 branches to the SJMP; Rev 20
                                   ;     branched straight to 0001$
        inc   0x2e
    0004$:
        sjmp  0001$
    0003$:

        mov   0x22,#0xff           ; chain A all high, then clear three bits
        clr   0x10                 ; channel A source bit 0
        clr   0x13                 ; channel B source bit 0
        clr   0x1e                 ; IRAM 0x23.6 back low
        lcall _shiftreg_out8_p1hi

        clr   a
        mov   0x25,a               ; chain B payload, both bytes zero
        mov   0x23,a
        lcall _shiftreg_out16_p1
        ret
    __endasm;
}
