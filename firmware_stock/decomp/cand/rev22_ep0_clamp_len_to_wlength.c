// MATCH: image=rev22 addr=0x0D9E len=65 func=ep0_clamp_len_to_wlength cflags=--peep-file,firmware_stock/decomp/keil.peep

/* Clamp the pending EP0 IN transfer length to the host's wLength, and decide
 * whether a terminating short packet will be owed.
 *
 * Called once, from std_get_descriptor (rev22 0x01E6), immediately before
 * ep0_in_stage_and_go. On entry IRAM 0x09:0x0B holds the length the firmware
 * intends to send -- 0x09 LOW, 0x0B HIGH, proved by the borrow inside
 * ep0_in_send_chunk at rev22 0x0AE1 -- and XDATA 0xFF2E:0xFF2F hold the SETUP
 * packet's wLength, little endian as the bus delivered it.
 *
 * Intent:
 *   if (len > wLength) { len = wLength; short_wanted = 0; }
 *   if (len < wLength)   short_wanted = 1;
 *
 * short_wanted is BIT 0x0D, i.e. IRAM 0x21.5. BYTE 0x0D is g_class_tag, a
 * completely unrelated object; the two print identically in a listing.
 *
 * ================= THE BUG, STILL PRESENT IN REV 22 =================
 * The two comparisons disagree with each other. The first is a correct
 * big-endian compare: high bytes at 0x0DA3, low bytes consulted only on a tie
 * (0x0DAA). The second, at 0x0DC9, compares the LOW bytes first and consults
 * the high bytes only on a low-byte tie (0x0DD0) -- it treats the low byte as
 * the more significant one.
 *
 * Because the clamp above has already forced len <= wLength, the flag should
 * be set exactly when len < wLength. Instead the two disagree precisely when
 * len_hi < wLength_hi AND len_lo > wLength_lo, where the flag is wrongly left
 * CLEAR. The standard GET_DESCRIPTOR(CONFIGURATION) re-read reaches it: the
 * 54-byte configuration (len 0x0036) against a host wLength of 0x0100 gives
 * len_lo 0x36 > wLength_lo 0x00, so no terminating short packet is flagged.
 *
 * REV 20 -> REV 22 DELTA: none. All 65 bytes at rev22 0x0D9E are identical to
 * rev20 0x0D6B, verified by comparing the two images byte for byte. Rev 22 did
 * not fix this. Full write-up:
 * firmware_stock/decomp/FINDING_clamp_byteswap.md
 * ====================================================================
 *
 * WHY ASSEMBLY. Same reason as the Rev 20 candidate, and it is not a codegen
 * nicety: SDCC compiles the equivalent C (reproduced at the end of the Rev 20
 * file, cand/ep0_clamp_len_to_wlength.c) to the same eleven instructions in
 * the same order -- including the swap -- but 71 bytes rather than 65, with
 * the excess spread over four separate offsets, every one of them Keil keeping
 * DPTR live where SDCC reloads it. The declared-partial mechanism permits one
 * offset, not four, and closing it would need a peephole rule per instruction
 * adjacency, which README.md says to write as assembly instead. */

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
        movx  a,@dptr           ; DPTR still 0xFF2F -- Keil kept it live
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
        clr   0x0d              ; exactly wLength: nothing short about it

        ;; ---- if (len < wLength) : BYTE-SWAPPED, see above ----------------
    00402$:
        mov   dptr,#0xff2e      ; SETUP wLength, LOW byte -- compared first
        movx  a,@dptr
        mov   r7,a
        mov   a,0x09
        clr   c
        subb  a,r7
        jc    00403$            ; len_lo < wLength_lo -> flag, highs ignored
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
