// MATCH: image=rev22 addr=0x092A len=140 func=keil_c51_startup cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Keil C51 runtime startup, and the ?C_INITSEG initializer interpreter it
 * hands off to -- Rev 22. Reset (the LJMP at 0x0000) lands here; the last
 * thing this run of code does is LJMP main_loop.
 *
 * PORTED FROM cand/c51_startup.c (rev20 0x0A09) VERBATIM apart from the two
 * immediates called out below. It matched on the first build.
 *
 * WRITTEN AS ONE __naked BLOB, and covering what Ghidra lists as two
 * functions -- `keil_c51_startup` (rev22 0x092A, 15 B) and
 * `keil_c_init_interpreter` (rev22 0x0939, 117 B of instructions plus an
 * 8-byte table Ghidra excludes). Three reasons, in order of force:
 *
 *   1. This is hand-written Keil library assembly (STARTUP.A51 / INIT.A51).
 *      There is no C to recover -- it interprets a byte-code, uses XCH to
 *      swap a 16-bit pointer through DPTR, and dispatches on the carry flag
 *      left behind by an ADD three instructions earlier. No compiler emits it.
 *   2. The pieces are not separable. The interpreter's terminator test at
 *      rev22 0x0978 is `JZ 0x0936`, a two-byte *relative* branch into what
 *      Ghidra calls keil_c51_startup. A relative branch cannot reference an
 *      external symbol, so a candidate covering only 0x0939.. cannot even be
 *      assembled, let alone matched. The 0x0939 boundary is an artifact of
 *      Ghidra naming the target of the `JZ 0x098F` branch.
 *   3. The 8-byte bit-mask table at rev22 0x0969 sits *inside* the code and is
 *      read with MOVC A,@A+PC, so its position relative to 0x095C is
 *      load-bearing. Only emitting code and table together pins that.
 *
 * REV 20 -> REV 22 DELTA: no behaviour change. The whole 140-byte run is
 * byte-identical to rev20 0x0A09..0x0A94 except for FIVE bytes, at run offsets
 * 0x08, 0x0A, 0x0B, 0x0E and 0x49, and nowhere else (diffed directly from the
 * two .bin images):
 *   +0x08  MOV SP,#imm            rev20 0x33          rev22 0x32
 *   +0x0A..0x0B  LJMP ?C_START    rev20 0x0A50        rev22 0x0971
 *   +0x0E  LJMP main (low byte)   rev20 0x0A95        rev22 0x0A3F
 *   +0x49  MOV DPTR,#?C_INITSEG   rev20 0x0F9C        rev22 0x0FBA
 * (the LJMP main high byte is 0x0A in both, so only its low byte differs.)
 * Three of the five are relocations. The one substantive change is SP: rev22
 * sets it to 0x32 rather than 0x33, i.e. it reserves ONE BYTE LESS of IRAM
 * below the stack. That is the linker reporting a one-byte-smaller DATA/IDATA
 * high-water mark, not a deliberate edit -- rev22 uses one fewer byte of
 * Keil-allocated scratch than rev20 did.
 *
 * The ?C_INITSEG table itself is unchanged in content: rev20 0x0F9C..0x0FC3
 * and rev22 0x0FBA..0x0FE1 compare equal byte for byte (13 one-byte DATA
 * records plus the 0x00 terminator). So the set of initialised variables and
 * their values is identical between the two firmwares.
 */
void keil_c51_startup(void) __naked {
    __asm
        .globl _main_loop

    ;; ---- ?C_STARTUP (rev22 0x092A) ------------------------------------------
    ;; Zero IRAM 0x7F..0x01. R0 starts at 0x7F, and DJNZ leaves the loop when
    ;; R0 reaches 0, so IRAM 0x00 (R0 of bank 0) is the one byte not cleared.
        mov   r0,#0x7f
        clr   a
    0001$:
        mov   @r0,a
        djnz  r0,0001$
        mov   0x81,#0x32           ; SP = 0x32, so the C51 stack grows from
                                   ; 0x33 up. Everything below is register
                                   ; banks, bit space and Keil DATA/IDATA.
                                   ; rev20 reserved one byte more (SP = 0x33).
        ljmp  0004$                ; -> ?C_START, the initializer entry

    ;; ---- ?C_INIT exit (rev22 0x0936) --------------------------------------
    ;; Reached from the interpreter terminator test below, which is why it
    ;; is a 2-byte relative branch and why this stub sits here rather than at
    ;; the end.
    0002$:
        ljmp  _main_loop           ; rev22 0x0A3F (rev20: 0x0A95)

    ;; ---- record type 00/10: fill consecutive DATA or PDATA bytes -------
    ;; Entered with R7 = byte count and CY = destination space:
    ;;   CY=0 -> MOV  @R0,A  (IDATA/DATA)
    ;;   CY=1 -> MOVX @R0,A  (PDATA, the 8-bit-address XDATA page)
    ;; Nothing between the ADD that set CY and the JC below disturbs it --
    ;; MOVC, INC DPTR, INC R0 and DJNZ all leave the carry alone.
    0003$:                          ; rev22 0x0939
        clr   a
        movc  a,@a+dptr            ; destination address (8-bit)
        inc   dptr
        mov   r0,a
    0009$:
        clr   a
        movc  a,@a+dptr            ; next initializer byte
        inc   dptr
        jc    0010$
        mov   @r0,a
        sjmp  0011$
    0010$:
        movx  @r0,a
    0011$:
        inc   r0
        djnz  r7,0009$
        sjmp  0005$                ; back to the record loop

    ;; ---- record type 11: set or clear consecutive BITs ------------------
    ;; Each payload byte is  v b6..b0 : bit 7 is the value, bits 6..0 the
    ;; 8051 bit address. The byte holding bit N is 0x20 + (N >> 3), and the
    ;; mask is 1 << (N & 7), fetched from the table at rev22 0x0969.
    0012$:                          ; rev22 0x094B
        clr   a
        movc  a,@a+dptr
        inc   dptr
        mov   r0,a                 ; R0 = raw record byte
        anl   a,#0x07              ; A = bit number within the byte
        add   a,#0x0c              ; + distance from the MOVC PC to the table
        xch   a,r0                 ; R0 = table index, A = raw record byte
        clr   c
        rlc   a                    ; CY = bit 7 = the value to write
        swap  a
        anl   a,#0x0f              ; A = (bitaddr >> 3)
        orl   a,#0x20              ; A = 0x20 + (bitaddr >> 3) = byte address
        xch   a,r0                 ; R0 = byte address, A = table index
        movc  a,@a+pc              ; A = 1 << (bitaddr & 7);  PC here = 0x095D
        jc    0013$
        cpl   a
        anl   a,@r0                ; value 0: clear the bit
        sjmp  0014$
    0013$:
        orl   a,@r0                ; value 1: set the bit
    0014$:
        mov   @r0,a
        djnz  r7,0012$
        sjmp  0005$

    ;; The MOVC A,@A+PC table. PC after the MOVC opcode is 0x095D and the
    ;; index carries +0x0C, so this must start at 0x0969 -- immediately after
    ;; the SJMP above, with no padding.
        .db   0x01, 0x02, 0x04, 0x08   ; split 4+4 only because the sdas
        .db   0x10, 0x20, 0x40, 0x80   ; listing wraps after 7 bytes

    ;; ---- ?C_START (rev22 0x0971) ---------------------------------------------
    0004$:
        mov   dptr,#0x0fba         ; ?C_INITSEG. rev22 0x0FBA (rev20 0x0F9C).
                                   ; The table is 13 one-byte DATA records
                                   ; (13 x 3 bytes = 39, then the 0x00
                                   ; terminator at 0x0FE1): 01 22 00, 01 20 00,
                                   ; 01 25 00, 01 23 00, 01 24 00, 01 21 00,
                                   ; 01 09 00, 01 0c 00, 01 0b 00, 01 0e 00,
                                   ; 01 0a 00, 01 0d 00, 01 08 03. Compared
                                   ; byte for byte against rev20's at 0x0F9C:
                                   ; identical, so the initialised-variable set
                                   ; did not change between the two revisions.
                                   ; It ends 01 08 03 -- IRAM 0x08 = 3, the
                                   ; 48 kHz clock mode that rev22's
                                   ; hw_clock_codec_init also writes, at 0x085C
                                   ; (MOV 0x08,#0x3).

    ;; ---- the record loop ------------------------------------------------
    ;; Record header byte:  tt cccccc
    ;;   tt = 00 DATA/IDATA fill, 10 PDATA fill, 01 XDATA fill, 11 BIT fill
    ;;   cccccc = element count, 1..0x1F direct. If bit 5 of the count is set
    ;;   the count is 16-bit: the low 5 bits are the high half and the next
    ;;   byte is the low half, decremented as a nested DJNZ pair.
    ;; A header byte of 0x00 terminates the table.
    0005$:                          ; rev22 0x0974
        clr   a
        mov   r6,#0x01             ; default count high half
        movc  a,@a+dptr
        jz    0002$                ; 0x00 -> table exhausted -> LJMP main
        inc   dptr
        mov   r7,a                 ; stash the raw header
        anl   a,#0x3f
        jnb   0xe5,0006$           ; ACC.5 clear -> 6-bit count, R6 stays 1
        anl   a,#0x1f
        mov   r6,a                 ; count high half
        clr   a
        movc  a,@a+dptr            ; count low half
        inc   dptr
        jz    0006$
        inc   r6                   ; low half non-zero: the nested DJNZ pair
                                   ; needs one extra pass of the outer loop
    0006$:
        xch   a,r7                 ; A = raw header, R7 = count low half
        anl   a,#0xc0              ; isolate the type field
        add   a,acc                ; 00->A=00 CY=0 | 01->A=80 CY=0
                                   ; 10->A=00 CY=1 | 11->A=80 CY=1
        jz    0003$                ; A==0: byte fill, CY picks DATA vs PDATA
        jc    0012$                ; A!=0 and CY: bit fill
                                   ; fall through: A!=0, CY=0 -> XDATA fill

    ;; ---- record type 01: fill consecutive XDATA bytes -------------------
    ;; The destination is a full 16-bit address, so it has to live in DPTR --
    ;; which already holds the table read pointer. The two are exchanged
    ;; around each MOVX through R2:R0 and A, three XCHs each way, which is why
    ;; this is the one record type with a visible cost per byte.
        clr   a                     ; rev22 0x0993
        movc  a,@a+dptr
        inc   dptr
        mov   r2,a                 ; destination high
        clr   a
        movc  a,@a+dptr
        inc   dptr
        mov   r0,a                 ; destination low
    0007$:
        clr   a
        movc  a,@a+dptr            ; initializer byte
        inc   dptr
        xch   a,r0                 ; swap R2:R0 into DPTR, preserving A
        xch   a,dpl
        xch   a,r0
        xch   a,r2
        xch   a,dph
        xch   a,r2
        movx  @dptr,a
        inc   dptr                 ; advance the destination
        xch   a,r0                 ; swap the table pointer back into DPTR
        xch   a,dpl
        xch   a,r0
        xch   a,r2
        xch   a,dph
        xch   a,r2
        djnz  r7,0007$             ; low half of the count
        djnz  r6,0007$             ; high half
        sjmp  0005$
    __endasm;
}
