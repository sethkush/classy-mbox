// MATCH: image=rev20 addr=0x0478 len=8 func=cmd6_set_cpt_mode1 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 6: set audio clock mode 1 (idle -- no sample clock).
 *
 * One of five near-identical event handlers that do nothing but call
 * audio_clock_mode_apply with a constant mode and fall out to the dispatcher
 * epilogue: 6 -> 1 (0x0478), 7 -> 2 (0x0480), 8 -> 3 (0x049A), 9 -> 4
 * (0x04B4), 10 -> 5 (0x04BC), 12 -> 1 again (0x0511). The mode numbering is
 * established by audio_clock_mode_apply itself, which stores it in IRAM 0x08
 * where the class GET_CUR handler reads it back.
 *
 * NAKED because the mode is a register parameter: Keil passes the first char
 * argument in R7, SDCC in DPL. `mov r7,#1` is two bytes, `mov dpl,#1` three,
 * so no arrangement of C reaches the stock encoding.
 *
 * The trailing LJMP is the `break` out of the dispatcher's switch. At source
 * level these handlers are case bodies of one function ending in
 * `g_event = 0;` at 0x0564; Keil emitted a long jump for the cases too far
 * from the epilogue for SJMP and a short one for the rest (cmd12 at 0x0516,
 * cmd11 at 0x050F, evt0d at 0x0524 all use SJMP).
 *
 * REV 22 CROSS-CHECK: Rev 22 merged events 6 and 12 into one body,
 * cmd6_12_set_clock_mode1 at rev22 0x0478 (XREFs from both jump-table slots
 * 0x031B and 0x032D), and hoisted the call itself into a tail at rev22 0x0512
 * shared with events 4, 5, 9 and 10. Mode 1 is unchanged. */
void cmd6_set_cpt_mode1(void) __naked {
    __asm
        .globl _audio_clock_mode_apply
        .globl _evt_dispatch_epilogue
        mov   r7,#0x01             ; clock mode 1 = idle
        lcall _audio_clock_mode_apply
        ljmp  _evt_dispatch_epilogue
    __endasm;
}
