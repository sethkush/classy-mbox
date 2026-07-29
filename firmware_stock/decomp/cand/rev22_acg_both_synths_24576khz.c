// MATCH: image=rev22 addr=0x0EC8 len=32 func=acg_both_synths_24576khz cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Load both adaptive clock generators with the 48 kHz-family frequency word
 * 0x61A80F, then fall through into acg2frq0_load_and_acgctl (0x0EE8) which
 * writes the last byte of the word and commits via ACGCTL = 0x06.
 *
 * Ghidra's name is a guess about what the word means and is not relied on
 * here.  What is verifiable from the images: this is the word loaded by clock
 * mode 3, and mode 3 is the mode IRAM 0x08 carries as 48 kHz (rev22 0x0772
 * `MOV 0x08,#3` immediately after the LCALL here).  Mode 2, 44100 Hz, uses
 * 0x6A4B20 instead.
 *
 * Three entry points, only the first of which is a "function":
 *   0x0EC7  sfr_write_then_acg_program -- one byte, `MOVX @DPTR,A`, commits a
 *           value the caller staged in A and falls in here.  Callers:
 *           audio_clock_set_mode 0x078A (GLOBCTL |= CPTEN) and
 *           hw_clock_codec_init 0x084F (CPTRXCNF4 = 0x03).
 *   0x0EC8  here.  Callers: audio_clock_set_mode 0x076F (mode 3) and
 *           audio_path_reconfig 0x09C4.
 *   0x0EE8  the tail, its own candidate; entered with A live.
 *
 * WRITTEN AS ASSEMBLY.  Rev 20's counterpart (acg_48k_commit, 0x0DEC, 43 bytes
 * spanning both halves) was plain C and matched, but Rev 22 reordered the last
 * ACG2FRQ0 store -- see the delta below -- into a form no C statement produces,
 * and the block also has to end WITHOUT a RET so it falls into 0x0EE8.
 *
 * ---------------------------------------------------------------------------
 * REV 20 -> REV 22 DELTA.  Same 43 bytes end to end (rev20 0x0DEC..0x0E16,
 * rev22 0x0EC8..0x0EF2), same frequency word, same registers, same order of
 * writes.  One instruction pair is transposed:
 *
 *     rev20 0x0E0C:  MOV DPTR,#0xFFF9 / MOV A,#0x0F / MOVX @DPTR,A
 *     rev22 0x0EE6:  MOV A,#0x0F / MOV DPTR,#0xFFF9 / MOVX @DPTR,A
 *
 * That transposition is the whole point: it moves the DPTR load to the far
 * side of the split, so the shared tail entry point (rev20 0x0E0F, rev22
 * 0x0EE8) now begins with `MOV DPTR,#0xFFF9` instead of with a bare
 * `MOVX @DPTR,A`.  Callers that jump into the tail therefore no longer have to
 * load DPTR themselves, which is where three of the six bytes Rev 22 saves in
 * audio_clock_set_mode come from.  No behavioural change.
 */
void acg_both_synths_24576khz(void) __naked {
    __asm
        mov   dptr,#0xffe6         ; ACG1FRQ1
        mov   a,#0xa8
        movx  @dptr,a
        mov   dptr,#0xffe5         ; ACG1FRQ2
        mov   a,#0x61
        movx  @dptr,a
        mov   dptr,#0xffe7         ; ACG1FRQ0
        mov   a,#0x0f
        movx  @dptr,a
        mov   dptr,#0xfff8         ; ACG2FRQ1
        mov   a,#0xa8
        movx  @dptr,a
        mov   dptr,#0xfff7         ; ACG2FRQ2
        mov   a,#0x61
        movx  @dptr,a
        mov   a,#0x0f              ; ACG2FRQ0 value, left live in A across the
                                   ;   function boundary; no RET here --
                                   ;   execution falls into 0x0EE8
    __endasm;
}
