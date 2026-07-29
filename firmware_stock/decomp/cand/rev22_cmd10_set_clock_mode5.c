// MATCH: image=rev22 addr=0x04C4 len=4 func=cmd10_set_clock_mode5 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 10, Rev 22: audio clock mode 5. Dispatched from jump-table entry 9
 * (rev22 0x0327 LJMP 0x04C4; rev20 0x031B LJMP 0x04BC).
 *
 * What the bytes give: mode 5 is the branch of audio_clock_set_mode that drops
 * GLOBCTL.CPTEN, rewrites CPTRXCNF4 (0xFFD4) with 0x01 instead of the 0x03
 * that the master init programs, and puts CPTEN back -- rev22 0x0777..0x0789
 * inside audio_clock_set_mode (0x070F), against hw_clock_codec_init's write at
 * rev22 0x084A; rev20 0x0799..0x07AB inside audio_clock_mode_apply (0x0728),
 * against hw_master_init's write at rev20 0x0929. Both images are byte-
 * identical here apart from the call operands.
 *
 * INFERENCE, NOT IN THE BYTES: I read that lowered divider as the receive side
 * being clocked from an external bit clock rather than the internal one, which
 * is why this mode is usually described as the externally clocked / S/PDIF-
 * slaved mode. Nothing in either image states that; the images only show the
 * divider value change and the ACG2DCTL / shift-register writes that go with
 * it. cand/cmd5_variantB_set_mode1.c hedges its reading of mode 1 the same
 * way, and notes that nothing traced so far actually requests mode 5.
 *
 * NAKED for the R7 register argument; see rev22_cmd6_12_set_clock_mode1.c.
 *
 * REV 20 -> REV 22 DELTA: BEHAVIOUR IDENTICAL, TAIL SHARED. Rev 20 was 8 bytes
 * at 0x04BC; Rev 22 is 4 (`MOV R7,#5 / SJMP 0x0512`). Mode 5 is unchanged. */
void cmd10_set_clock_mode5(void) __naked {
    __asm
        mov   r7,#0x05
        /* `sjmp _evt_tail_apply_clock_mode` (0x0512), self-relative: `.` is
         * area-relative, so the displacement is the constant 0x4A at assembly
         * time and survives relocation: 0x04C6 + 2 + 0x4A = 0x0512. */
        sjmp  . + (0x0512 - 0x04c6)
    __endasm;
}
