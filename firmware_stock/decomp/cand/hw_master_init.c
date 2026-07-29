// MATCH: image=rev20 addr=0x08CB len=165 func=hw_master_init cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Master hardware bring-up. Run once from main() before USB is attached.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY. Keil allocated the accumulator across the
 * whole function -- A is loaded with 0 once at entry and stays live through
 * fifteen instructions, with INC A used to produce the 1 for MEMCFG, and DPTR
 * is likewise carried from 0xFFB0 to 0xFFB1 via a 1-byte INC DPTR across
 * fifteen unrelated SFR writes. That is global register allocation, not a
 * peephole-sized difference: SDCC re-zeroes A after every store and reloads
 * DPTR every time. Reproducing it in C would need one narrow adjacency rule
 * per context, which would not generalise and would put the already-matching
 * functions at risk. The content here is pure register programming, so the
 * assembly carries the meaning as well as C would.
 *
 * Ordering that matters:
 *   - USBCTL is cleared FIRST, so the host cannot enumerate the device while
 *     the codec port is half-configured.
 *   - GLOBCTL.CPTEN is set LAST, after every CPTCNF/CPTRXCNF register, because
 *     those registers are only writable while the codec port is disabled.
 */
void hw_master_init(void) __naked {
    __asm
        .globl _sfr_store_then_acg_48k
        .globl _acg_incdptr_dctl_div2
        .globl _shiftreg8_commit
        .globl _shiftreg16_commit

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
        mov   a,#0x03              ;   DIVB2 = /4; the mode-5 branch uses 0x01

        lcall _sfr_store_then_acg_48k  ; commits the pending MOVX, then loads
                                       ; both synthesizers with the 48 k word
        lcall _acg_incdptr_dctl_div2   ; ACG1DCTL = ACG2DCTL = 0x10  (/2)

        mov   dptr,#0xffb1         ; GLOBCTL
        movx  a,@dptr
        orl   a,#0x01              ; CPTEN, only now that the codec port is set
        movx  @dptr,a
        mov   0x08,#0x03           ; current clock mode = 3 = 48 kHz

        clr   a
        mov   0x22,a               ; chain A payload = 0
        setb  0x1e                 ; IRAM 0x23.6 high while settling
        lcall _shiftreg8_commit

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
        jnz   0001$
        inc   0x2e
        sjmp  0001$
    0003$:

        mov   0x22,#0xff           ; chain A all high, then clear three bits
        clr   0x10                 ; channel A source bit 0
        clr   0x13                 ; channel B source bit 0
        clr   0x1e                 ; IRAM 0x23.6 back low
        lcall _shiftreg8_commit

        clr   a
        mov   0x25,a               ; chain B payload, both bytes zero
        mov   0x23,a
        lcall _shiftreg16_commit
        ret
    __endasm;
}
