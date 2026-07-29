// MATCH: image=rev22 addr=0x02F3 len=67 span=1 func=usb_deferred_action_dispatch cflags=--peep-file,firmware_stock/decomp/keil.peep
/* The deferred-work dispatcher, Rev 22: `switch (g_event)` over the pending
 * event code in IRAM 0x0A, fourteen dense cases and a default.
 *
 * Same construct as Rev 20's device_event_dispatch at 0x02EE, and the same
 * Ghidra artefact: the 42-byte jump table at 0x030C is not a function, it is
 * the switch's table, unreachable except through the `JMP @A+DPTR` that
 * precedes it. This candidate claims the whole 67-byte run 0x02F3..0x0335,
 * hence span=1.
 *
 * The event codes are 1..14, each posted elsewhere by a `MOV 0x0A,#n` --
 * usb_susr_handler at 0x0006 posts 14 in both images (`75 0a 0e`). Zero means
 * "nothing pending"; the epilogue at 0x0563 is `g_event = 0`, so the default
 * arm both catches an out-of-range code and is the normal way the queue is
 * cleared after a case body finishes.
 *
 * The range check is unchanged from Rev 20 and is Keil's standard unsigned
 * window test: A = g_event - 1; if (A >= 14) goto default. `CJNE A,#0x0E,$+3`
 * has displacement zero, so both exits land on the next instruction -- the
 * branch exists only for the carry it leaves (C = A < 14).
 *
 * REV 20 -> REV 22 DELTA: BEHAVIOUR UNCHANGED, INDEX SCALING RE-CODED.
 * This is the one place in this batch where the emitted code genuinely
 * differs rather than merely relocating, and it is a codegen difference, not
 * a semantic one.
 *
 *   rev20 0x02FB   MOV DPTR,#0x0300 / MOV R0,A / ADD A,R0 / ADD A,R0
 *                  / JMP @A+DPTR                                 (7 bytes)
 *   rev22 0x02FE   MOV DPTR,#0x030C / MOV B,#0x03 / MUL AB
 *                  / XCH A,DPH / ADD A,B / XCH A,DPH
 *                  / JMP @A+DPTR                                 (14 bytes)
 *
 * Rev 20 forms index*3 by two 8-bit adds and offsets DPTR only through A, so
 * the table must not cross a 256-byte boundary and the product must stay under
 * 256. Rev 22 uses MUL AB, which leaves the 16-bit product in B:A, and folds
 * the high half into DPH with the XCH/ADD/XCH dance -- a full 16-bit index add
 * that works for any table position and any case count up to 85. Rev 22's
 * table at 0x030C ends at 0x0335, so it does not straddle a page and the
 * widened arithmetic buys nothing here; with fourteen cases the largest index
 * is 13 and the product is 39, so B is always zero and the added high half is
 * always zero. Both sequences compute the same address for every reachable
 * input. This is the general-form dense-switch expansion where Rev 20 got the
 * narrow one -- an optimiser decision, seven bytes of cost, no behavioural
 * consequence.
 *
 * THE TABLE ITSELF CHANGED IN ONE SLOT, AND THAT IS A REAL CHANGE.
 * Rev 20 entries 6 and 12 point at two separate copies of the same body
 * (0x0478 and 0x0511); Rev 22 entries 6 and 12 both point at 0x0478. Ghidra
 * names the Rev 22 body cmd6_12_set_clock_mode1 and records XREFs from both
 * slots 0x031B and 0x032D. That is `case 6: case 12:` sharing one body where
 * Rev 20 duplicated it -- again codegen, not behaviour: both revisions run
 * clock mode 1 for either code.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY, for the same reason as the Rev 20
 * counterpart: SDCC's dense-switch lowering is a structurally different
 * object (two parallel byte tables read with MOVC A,@A+PC, then a run of
 * LJMPs), no peephole rewrites one form into the other, and the case bodies
 * are twelve other functions so there is no C text that would produce this. */
void usb_deferred_action_dispatch(void) __naked {
    __asm
        .globl _cmd1_apply_clock_mode
        .globl _cmd2_apply_iface1_alt
        .globl _cmd3_apply_iface2_alt
        .globl _cmd4_reapply_current_clock_mode
        .globl _cmd5_set_clock_mode1_altbits
        .globl _cmd6_12_set_clock_mode1
        .globl _cmd7_set_clock_mode2_prog_spdif
        .globl _cmd8_set_clock_mode3_prog_spdif
        .globl _cmd9_set_clock_mode4
        .globl _cmd10_set_clock_mode5
        .globl _cmd11_eeprom_selftest
        .globl _cmd13_invalidate_boot_eeprom
        .globl _cmd14_usb_suspend_and_resume
        .globl _evt_dispatch_epilogue

        mov   a,0x0a               ; BYTE 0x0A = g_event, the pending code
        dec   a                    ; codes are 1-based; A = index 0..13
        cjne  a,#0x0e,0001$        ; displacement 0: wanted only for the carry
    0001$:
        jc    0002$                ; C set <=> index < 14 <=> code in 1..14
        ljmp  _evt_dispatch_epilogue   ; 0x0563: g_event = 0, and return

    0002$:
        /* Table base written as a literal: link51 generates an absolute equate
         * for every Ghidra function name, and the table is inside this one, so
         * a locally defined symbol would collide. Same reason as Rev 20. */
        mov   dptr,#0x030c         ; event jump table
        mov   b,#0x03              ; 3 bytes per entry (one LJMP)
        mul   ab                   ; B:A = index * 3  (B is always 0 here)
        xch   a,dph                ; fold the product's high half into DPH
        add   a,b
        xch   a,dph
        jmp   @a+dptr

        ;; ---- event jump table @ 0x030C ------------------------------------
        ;; Entry n is reached for g_event == n+1. Every case is a tail call:
        ;; the case body's last act is the epilogue, so control never returns
        ;; here.
        ljmp  _cmd1_apply_clock_mode              ; 1  -> 0x0336
        ljmp  _cmd2_apply_iface1_alt              ; 2  -> 0x038A  SET_INTERFACE ifc 1
        ljmp  _cmd3_apply_iface2_alt              ; 3  -> 0x03FD  SET_INTERFACE ifc 2
        ljmp  _cmd4_reapply_current_clock_mode    ; 4  -> 0x045A
        ljmp  _cmd5_set_clock_mode1_altbits       ; 5  -> 0x0469
        ljmp  _cmd6_12_set_clock_mode1            ; 6  -> 0x0478
        ljmp  _cmd7_set_clock_mode2_prog_spdif    ; 7  -> 0x047D
        ljmp  _cmd8_set_clock_mode3_prog_spdif    ; 8  -> 0x049F
        ljmp  _cmd9_set_clock_mode4               ; 9  -> 0x04C0
        ljmp  _cmd10_set_clock_mode5              ; 10 -> 0x04C4
        ljmp  _cmd11_eeprom_selftest              ; 11 -> 0x04C8
        ljmp  _cmd6_12_set_clock_mode1            ; 12 -> 0x0478  (shared with 6)
        ljmp  _cmd13_invalidate_boot_eeprom       ; 13 -> 0x0517
        ljmp  _cmd14_usb_suspend_and_resume       ; 14 -> 0x0525
    __endasm;
}
