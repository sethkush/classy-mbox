// MATCH: image=rev20 addr=0x04BC len=8 func=cmd10_set_cpt_mode5 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 10: audio clock mode 5. Dispatched from event_jump_table entry 9
 * (rev20 0x031B LJMP 0x04BC, rev22 0x0327 LJMP 0x04C4).
 *
 * What the bytes give: mode 5 is the branch of audio_clock_mode_apply that
 * drops GLOBCTL.CPTEN, rewrites CPTRXCNF4 (0xFFD4) with 0x01 instead of the
 * 0x03 hw_master_init programs, and puts CPTEN back: rev20 0x0799..0x07AB
 * inside audio_clock_mode_apply (0x0728), against hw_master_init's write at
 * rev20 0x0929; rev22 0x0777..0x0789 inside audio_clock_mode_apply (0x070F),
 * against hw_master_init's write at rev22 0x084A. Both images are byte-
 * identical here apart from the LCALL operand.
 *
 * INFERENCE, NOT IN THE BYTES: I read that lowered DIVB2 as the receive side
 * being clocked from an external bit clock rather than the internal one, which
 * is why this mode is usually described as the externally clocked / S/PDIF-
 * slaved mode. Nothing in either image states that; the images only show the
 * divider value change and the ACG2DCTL / shift-register writes that go with
 * it. cmd5_variantB_set_mode1.c hedges its own reading of mode 1 the same way,
 * and notes that nothing traced so far actually requests mode 5.
 *
 * NAKED for the same reason as cmd6. See cmd6_set_cpt_mode1.c.
 *
 * REV 22 CROSS-CHECK: rev22 0x04C4 is four bytes, `MOV R7,#0x05; SJMP 0x0512`
 * (7f 05 80 4a) -- Rev 22 hoisted the LCALL/epilogue into the shared tail at
 * 0x0512 that events 4, 5, 6/12, 9 and 10 all reach. Mode 5 is unchanged. */
void cmd10_set_cpt_mode5(void) __naked {
    __asm
        .globl _audio_clock_mode_apply
        .globl _evt_dispatch_epilogue
        mov   r7,#0x05
        lcall _audio_clock_mode_apply
        ljmp  _evt_dispatch_epilogue
    __endasm;
}
