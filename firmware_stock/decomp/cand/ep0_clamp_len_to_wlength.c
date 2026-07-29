// MATCH: image=rev20 addr=0x0D6B len=65 func=ep0_clamp_len_to_wlength cflags=--peep-file,firmware_stock/decomp/keil.peep

/* Clamp the pending EP0 IN transfer length to the host's wLength, and decide
 * whether a terminating short packet will be owed.
 *
 * Called once, from 0x01EB in the GET_DESCRIPTOR path, immediately before
 * ep0_in_start_transfer. On entry IRAM 0x09:0x0B holds the length the firmware
 * wants to send -- 0x09 LOW, 0x0B HIGH, proved by the borrow at 0x0BB0 inside
 * ep0_in_fill_chunk -- and XDATA 0xFF2E:0xFF2F holds the SETUP packet's
 * wLength, little endian as the bus delivers it.
 *
 *   if (len > wLength) { len = wLength; short_wanted = 0; }
 *   if (len < wLength)   short_wanted = 1;
 *
 * where short_wanted is BIT 0x0D (IRAM 0x21.5). Note BYTE 0x0D is g_class_tag,
 * an unrelated object; the two print identically in a listing.
 *
 * ================= THE BUG =================
 * The two comparisons do not agree with each other. The first is a correct
 * big-endian compare: high bytes at 0x0D70, and the low bytes are consulted
 * only on a tie (0x0D77). The second, at 0x0D96, compares the LOW bytes first
 * and consults the high bytes only on a tie (0x0D9D) -- it treats the low byte
 * as the more significant one, so short_wanted comes out backwards whenever
 * the two lengths differ in the high byte but the low bytes decide.
 * Length 0x0100 against wLength 0x00FF sets the flag; 0x00FF against 0x0100
 * does not. Both are wrong.
 *
 * It is in both images. Rev 22 has this function at 0x0D9E and all 65 bytes
 * are identical to Rev 20's -- the swap was not fixed. Practical reach is
 * small because EP0 traffic here is descriptors and short class replies, so
 * both high bytes are usually zero and the swapped compare degenerates to the
 * correct one. It is reachable through GET_DESCRIPTOR(CONFIGURATION) with
 * wLength >= 0x0100. Full write-up: firmware_stock/decomp/FINDING_clamp_byteswap.md
 * ===========================================
 *
 * WHY ASSEMBLY. The C for this is short and obvious, and SDCC compiles it to
 * the same eleven instructions in the same order -- including, when the
 * comparison is written byte-swapped, the bug. It comes out 71 bytes against
 * stock's 65 (measured, SDCC 4.6.0 with keil.peep), and the excess is at four
 * separate places, every one of them Keil keeping DPTR live where SDCC
 * reloads it: the re-read of wLengthH at 0x0D76 with a bare MOVX (+3), the
 * `INC DPTR` walks at 0x0D8B (+2) and 0x0DA0 (+2), and the re-read of
 * wLengthL at 0x0D9C (+3). Four bytes come back because SDCC finds a shorter
 * shape for two of the compares, so the net is +6 rather than +10.
 * The declared-partial mechanism cannot express that -- it allows one
 * offset, not four -- and closing it would need a peephole rule per specific
 * instruction adjacency, which is the case README.md says to write as
 * assembly instead. The C that was tried is reproduced in the comment at the
 * end of this file so the shape is on record. */

void ep0_clamp_len_to_wlength(void) __naked {
    __asm
        ;; ---- if (len > wLength) : correct, high byte first ----------------
        mov   dptr,#0xff2f      ; SETUP wLength, high byte
        movx  a,@dptr
        mov   r7,a
        mov   a,0x0b            ; len high
        setb  c
        subb  a,r7              ; len_hi - wLength_hi - 1
        jnc   00401$            ; len_hi >  wLength_hi -> clamp
        movx  a,@dptr           ; DPTR still 0xFF2F
        cjne  a,0x0b,00402$     ; len_hi <  wLength_hi -> leave it alone
        mov   dptr,#0xff2e      ; equal high bytes: tie-break on the low ones
        movx  a,@dptr
        mov   r7,a
        mov   a,0x09            ; len low
        setb  c
        subb  a,r7
        jc    00402$            ; len_lo <= wLength_lo -> leave it alone
    00401$:
        mov   dptr,#0xff2e      ; len = wLength
        movx  a,@dptr
        mov   0x09,a
        inc   dptr
        movx  a,@dptr
        mov   0x0b,a
        clr   0x0d              ; nothing short about it: we send exactly wLength

        ;; ---- if (len < wLength) : BYTE-SWAPPED, see above ----------------
    00402$:
        mov   dptr,#0xff2e      ; SETUP wLength, LOW byte -- compared first
        movx  a,@dptr
        mov   r7,a
        mov   a,0x09
        clr   c
        subb  a,r7
        jc    00403$            ; len_lo < wLength_lo -> flag, ignoring the highs
        movx  a,@dptr
        cjne  a,0x09,00404$     ; low bytes differ the other way -> no flag
        inc   dptr              ; only on a low-byte TIE is the high byte read
        movx  a,@dptr
        mov   r7,a
        mov   a,0x0b
        clr   c
        subb  a,r7
        jnc   00404$
    00403$:
        setb  0x0d              ; host asked for more than we have: a short
                                ; packet will terminate the data stage
    00404$:
        ret
    __endasm;
}

/* The C that produces the same eleven instructions, eight bytes longer:
 *
 *   SFRX(SETUP_wLengthL, 0xFF2E);
 *   SFRX(SETUP_wLengthH, 0xFF2F);
 *   __data __at (0x09) unsigned char g_xfer_len_lo;
 *   __data __at (0x0B) unsigned char g_xfer_len_hi;
 *   __bit  __at (0x0D) f_short_wanted;
 *
 *   void ep0_clamp_len_to_wlength(void) {
 *       if (g_xfer_len_hi > SETUP_wLengthH ||
 *           (SETUP_wLengthH == g_xfer_len_hi &&
 *            g_xfer_len_lo > SETUP_wLengthL)) {
 *           g_xfer_len_lo = SETUP_wLengthL;
 *           g_xfer_len_hi = SETUP_wLengthH;
 *           f_short_wanted = 0;
 *       }
 *       if (g_xfer_len_lo < SETUP_wLengthL ||          // <- the swap
 *           (SETUP_wLengthL == g_xfer_len_lo &&
 *            g_xfer_len_hi < SETUP_wLengthH))
 *           f_short_wanted = 1;
 *   }
 */
