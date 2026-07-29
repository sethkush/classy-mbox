// MATCH: image=rev22 addr=0x01ED len=66 func=std_get_interface cflags=--peep-file,firmware_stock/decomp/keil.peep

/* GET_INTERFACE, Rev 22 (rev22 0x01ED, 66 B; rev20 0x01F1, 62 B).
 *
 * Replies with one byte: the alternate setting currently selected for the
 * interface named in wIndex.  BEHAVIOUR IS IDENTICAL TO REV 20 -- same guard
 * order, same three arms, same reply values 1 / 2 / 0.
 *
 * The guard rejects wIndex > 2, not >= 2: `SETB C / SUBB A,#2 / JNC` borrows
 * for wIndex 0, 1 and 2, so all three reach the reply code.  wIndex 0 falls
 * into the else arm and answers 0, which is correct -- interface 0 has only
 * alternate setting 0.
 *
 * f_cfg_alt (bit 0x0A, IRAM 0x21.2) is written by SET_CONFIGURATION and
 * SET_INTERFACE-adjacent code but never SET anywhere in either image, so the
 * `JB 0x0a` always falls through to the f_configured test.  Vestigial, and
 * still costing three bytes in Rev 22.
 *
 * Bit addresses used (bit B is IRAM 0x20+(B>>3), bit B&7):
 *   0x08 f_iface1_alt (0x21.0)   0x09 f_iface2_alt (0x21.1)
 *   0x0A f_cfg_alt    (0x21.2)   0x0E f_configured (0x21.6)
 *
 * WHY THIS IS ASSEMBLY AND NOT C.  Two independent reasons, both structural:
 *
 *  1. DPTR LIVE ACROSS A CALL.  At 0x01FC stock calls ep0_in_buf_ptr_load
 *     (0x0B37), which writes only IRAM 0x1D/0x1E, and at 0x01FF re-reads
 *     SETUP_wIndexL with a bare `MOVX A,@DPTR` -- DPTR still holds 0xFF2C
 *     from the range check.  Keil's inter-procedural register analysis knew
 *     the callee left DPTR alone; SDCC has none and reloads it.  That is
 *     exactly the three-byte shortfall that made the Rev 20 counterpart a
 *     declared partial (cand/partial/std_get_interface.c, partial=3 at=0x12).
 *  2. The function ends `MOV DPTR,#0xFF6B / MOV A,#1 / SJMP 0x0247` -- a
 *     two-byte short jump into the middle of another function.  SDCC will not
 *     emit a short jump to an external symbol at all, so no amount of C or
 *     peephole work reaches those two bytes either.
 *
 * Written as annotated __naked assembly, it matches exactly; the Rev 20 file
 * remains the readable-C version of the same logic.  The trailing SJMP uses a
 * `.`-relative displacement so it is correct wherever the function is placed.
 *
 * REV 20 -> REV 22 DELTA (encoding only; +4 bytes, 62 -> 66):
 *   rev20 0x0229  LJMP 0x0B45 (ep0_send_1byte)                   3 B
 *   rev22 0x0225  MOV DPTR,#0xFF6B / MOV A,#1 / SJMP 0x0247      7 B
 * Rev 20's ep0_send_1byte (0x0B45) was
 *   MOV DPTR,#0xFF6B / MOV A,#1 / MOVX @DPTR,A / CLR 0x0B / SETB 0x0C / RET.
 * Rev 22 split it: the SFR address and the byte count are inlined at each
 * caller and only the generic tail survives as ep0_arm_in_and_done (0x0247,
 * MOVX @DPTR,A / CLR 0x0B / SETB 0x0C / RET), shared by three callers that
 * want counts 3, 2 and 1 (0x0108, 0x0174, here).  Rev 20 could not share it
 * because the count was baked into the helper.
 *
 * Helpers renumbered by the image shift, same code in both:
 *   ep0_in_buf_ptr_load   rev20 0x0B3E (IRAM 0x1B/0x1C) -> rev22 0x0B37 (0x1D/0x1E)
 *   ep0_load_dptr         rev20 0x0B17                  -> rev22 0x0B25
 *   ep0_stall_both        rev20 0x1009                  -> rev22 0x02EF
 * Note the EP0 buffer pointer moved IRAM location between the images (0x1B/0x1C
 * -> 0x1D/0x1E) while the buffer itself is 0xFA18 in both.
 *
 * 0xFF6B is IEPDCNTX0, the input-endpoint-0 data count register: writing 1
 * arms a one-byte IN transfer from the EP0 IN buffer.
 */
void std_get_interface(void) __naked {
    __asm
        .globl _ep0_in_buf_ptr_load    ; rev22 0x0B37
        .globl _ep0_load_dptr          ; rev22 0x0B25
        .globl _ep0_stall_both         ; rev22 0x02EF
        ;; ---- must be configured ----------------------------------------
        jb    0x0a,0001$          ; f_cfg_alt -- never set; always falls through
        jnb   0x0e,0009$          ; not configured -> stall
    0001$:
        ;; ---- wIndex must name interface 0, 1 or 2 ----------------------
        mov   dptr,#0xff2c        ; SETUP_wIndexL
        movx  a,@dptr
        setb  c
        subb  a,#0x02
        jnc   0009$               ; wIndex > 2 -> stall

        ;; ---- point the EP0 IN pointer at the reply buffer --------------
        lcall _ep0_in_buf_ptr_load   ; IRAM 0x1D:0x1E = 0xFA18; leaves DPTR alone
        movx  a,@dptr             ; DPTR is STILL 0xFF2C -- re-read wIndex

        ;; ---- interface 1 -----------------------------------------------
        cjne  a,#0x01,0002$
        jnb   0x08,0002$          ; interface 1 on alt 0 -> answer 0
        lcall _ep0_load_dptr      ; DPTR = IRAM 0x1D:0x1E, the EP0 IN buffer
        mov   a,#0x01
        movx  @dptr,a
        sjmp  0008$

        ;; ---- interface 2 -----------------------------------------------
    0002$:
        mov   dptr,#0xff2c
        movx  a,@dptr
        cjne  a,#0x02,0003$
        jnb   0x09,0003$          ; interface 2 on alt 0 -> answer 0
        lcall _ep0_load_dptr
        mov   a,#0x02
        movx  @dptr,a
        sjmp  0008$

        ;; ---- interface 0, or an interface sitting on alt 0 -------------
    0003$:
        lcall _ep0_load_dptr
        clr   a
        movx  @dptr,a

        ;; ---- arm a 1-byte IN transfer, then join the shared tail -------
    0008$:
        mov   dptr,#0xff6b        ; IEPDCNTX0
        mov   a,#0x01             ; one byte of reply
        sjmp  .+0x1d              ; -> ep0_arm_in_and_done at 0x0247

    0009$:
        ljmp  _ep0_stall_both
    __endasm;
}
