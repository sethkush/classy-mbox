// MATCH: image=rev20 addr=0x02EE len=60 span=1 func=device_event_dispatch cflags=--peep-file,firmware_stock/decomp/keil.peep
/* The deferred-work dispatcher: `switch (g_event)` over the pending event code
 * in IRAM 0x0A, with fourteen dense cases and a default.
 *
 * Ghidra splits this into two "functions" -- device_event_dispatch (0x02EE, 18
 * bytes) and event_jump_table (0x0300, 42 bytes). They are one construct: the
 * table is the switch's jump table and is unreachable except through the
 * `JMP @A+DPTR` eighteen bytes above it. This candidate claims the whole
 * 60-byte run 0x02EE..0x0329, which is why it carries span=1.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY, and not for a codegen-quality reason --
 * SDCC's dense-switch lowering is a structurally different object. Keil emits
 * one 3-byte LJMP per case and indexes it with key*3; SDCC emits two parallel
 * single-byte tables (low halves, then high halves) read with `MOVC A,@A+PC`
 * and then a separate run of LJMPs, i.e. 3 bytes per case plus 10 bytes of
 * prologue and a different addressing mode entirely. No peephole rule rewrites
 * one form into the other, and the case bodies here are twelve other
 * functions, so there is no C text that could produce this table anyway.
 *
 * The event codes are 1..14 and each is produced elsewhere by a `MOV 0x0A,#n`
 * -- e.g. usb_ev_suspend at 0x0006 sets 14 (rev20 0x0006 / rev22 0x0006, both
 * `75 0a 0e`). Zero means "nothing pending"; evt_dispatch_epilogue (0x0564) is
 * `g_event = 0`, so the default arm both handles an out-of-range code and is
 * the normal way the queue is cleared.
 *
 * The range check is Keil's standard unsigned window test:
 *     A = g_event - 1;   if (A >= 14) goto default;
 * expressed as DEC A / CJNE A,#14,$+3 / JC. The CJNE displacement is zero, so
 * both of its exits land on the next instruction -- the branch is there only
 * for the carry it leaves behind (C = A < 14). This is the same idiom the
 * README records for `x >= const`, with CJNE standing in for SETB C/SUBB.
 */
void device_event_dispatch(void) __naked {
    __asm
        .globl _cmd1_apply_clock_mode
        .globl _cmd2_apply_iface1_alt
        .globl _cmd3_apply_iface2_alt
        .globl _cmd4_variantA_reapply_mode
        .globl _cmd5_variantB_set_mode1
        .globl _cmd6_set_cpt_mode1
        .globl _cmd7_set_cpt_mode2_progchip
        .globl _cmd8_set_cpt_mode3_progchip
        .globl _cmd9_set_cpt_mode4
        .globl _cmd10_set_cpt_mode5
        .globl _cmd11_eeprom_selftest
        .globl _cmd12_set_cpt_mode1
        .globl _evt0d_invalidate_boot_eeprom
        .globl _evt0e_usb_suspend_enter_and_resume
        .globl _evt_dispatch_epilogue

        mov   a,0x0a               ; g_event, the pending event code
        dec   a                    ; codes are 1-based; A = index 0..13
        cjne  a,#0x0e,0001$        ; displacement 0: wanted only for the carry
    0001$:
        jc    0002$                ; C set  <=> index < 14  <=> code in 1..14
        ljmp  _evt_dispatch_epilogue   ; 0x0564: g_event = 0, and return

    0002$:
        ; The table base is written as a literal because the label below is
        ; deliberately not exported: link51 generates an absolute equate for
        ; every Ghidra function name, and `event_jump_table` is one of them, so
        ; defining the symbol here would collide with that equate.
        mov   dptr,#0x0300         ; event_jump_table
        mov   r0,a
        add   a,r0
        add   a,r0                 ; A = index*3, one LJMP per entry
        jmp   @a+dptr

        ;; ---- event_jump_table @ 0x0300 -------------------------------------
        ;; Entry n is reached for g_event == n+1. Every case is a tail call:
        ;; Keil folded "case body is one call, then leave the switch" into a
        ;; direct LJMP, so control never comes back here.
    0003$:
        ljmp  _cmd1_apply_clock_mode             ; 1  -> 0x032A
        ljmp  _cmd2_apply_iface1_alt             ; 2  -> 0x0386  SET_INTERFACE ifc 1
        ljmp  _cmd3_apply_iface2_alt             ; 3  -> 0x03FD  SET_INTERFACE ifc 2
        ljmp  _cmd4_variantA_reapply_mode        ; 4  -> 0x0454
        ljmp  _cmd5_variantB_set_mode1           ; 5  -> 0x0466
        ljmp  _cmd6_set_cpt_mode1                ; 6  -> 0x0478
        ljmp  _cmd7_set_cpt_mode2_progchip       ; 7  -> 0x0480
        ljmp  _cmd8_set_cpt_mode3_progchip       ; 8  -> 0x049A
        ljmp  _cmd9_set_cpt_mode4                ; 9  -> 0x04B4
        ljmp  _cmd10_set_cpt_mode5               ; 10 -> 0x04BC
        ljmp  _cmd11_eeprom_selftest             ; 11 -> 0x04C4
        ljmp  _cmd12_set_cpt_mode1               ; 12 -> 0x0511
        ljmp  _evt0d_invalidate_boot_eeprom      ; 13 -> 0x0518
        ljmp  _evt0e_usb_suspend_enter_and_resume; 14 -> 0x0526
    __endasm;
}
