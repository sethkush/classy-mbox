// MATCH: image=rev22 addr=0x0478 len=5 func=cmd6_12_set_clock_mode1 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Events 6 AND 12, Rev 22: set audio clock mode 1 (idle -- no sample clock).
 *
 * ONE BODY, TWO CASE LABELS. Jump-table slots 6 (0x031B) and 12 (0x032D) both
 * LJMP here, which is what `case 6: case 12:` looks like once the compiler
 * stops duplicating the body. Rev 20 emitted it twice, as cmd6_set_cpt_mode1
 * at 0x0478 and cmd12_set_cpt_mode1 at 0x0511 -- identical work, two copies.
 * That merge is the strongest single piece of evidence that all fourteen of
 * these "functions" are case bodies of one source function.
 *
 * One of the family of five handlers in Rev 22 that do nothing but hand a
 * constant mode to the shared tail at 0x0512: 6/12 -> 1 (0x0478), 9 -> 4
 * (0x04C0), 10 -> 5 (0x04C4), plus events 4 and 5 which reach the same tail
 * after their panel work. The mode numbering is established by
 * audio_clock_set_mode itself (0x070F), which stores it in IRAM byte 0x08
 * where the class GET_CUR handler reads it back.
 *
 * NAKED because the mode is a register parameter: Keil passes the first char
 * argument in R7, SDCC in DPL. `mov r7,#1` is two bytes, `mov dpl,#1` three,
 * so no arrangement of C reaches the stock encoding.
 *
 * REV 20 -> REV 22 DELTA: BEHAVIOUR IDENTICAL, TWO BODIES BECAME ONE.
 * Rev 20's cmd6 (8 B) and cmd12 (7 B) each did `MOV R7,#1 / LCALL
 * audio_clock_mode_apply` then LJMP resp. SJMP to the epilogue. Rev 22 has one
 * 5-byte body that jumps to the merged tail at 0x0512, which carries the
 * LCALL. Mode 1 is unchanged; the number of CS8427/ACG operations performed is
 * unchanged. */
void cmd6_12_set_clock_mode1(void) __naked {
    __asm
        .globl _evt_tail_apply_clock_mode
        mov   r7,#0x01             ; clock mode 1 = idle
        ljmp  _evt_tail_apply_clock_mode   ; 0x0512: apply it, clear the event
    __endasm;
}
