// MATCH: image=rev22 addr=0x0FE2 len=16 func=cport_cnf3_write_enable cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Write one value to both codec-port frame-config-3 registers and re-enable
 * the codec port.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY: it is entered with A already holding the
 * value to store, which no C51 calling convention produces (Keil passes the
 * first char parameter in R7).  Rev 22's callers load only A:
 *
 *      rev22 0x0356:  MOV A,#0xAC ; LCALL 0x0FE2   (0x0358)
 *      rev22 0x035E:  MOV A,#0xA8 ; LCALL 0x0FE2   (0x0360)
 *
 * What it does, in TAS1020B terms:
 *   CPTCNF3   (0xFFDE) = A       codec port frame config 3 -- transmit side
 *   CPTRXCNF3 (0xFFD5) = A       the receive-side mirror of the same field
 *   GLOBCTL   (0xFFB1) |= 0x01   set CPTEN, re-enabling the codec port
 *
 * The GLOBCTL read-modify-write is the ordering that matters: CPTCNF/CPTRXCNF
 * are only writable while CPTEN is clear, so every caller drops CPTEN, pokes
 * the config, and comes here to bring the port back.  hw_clock_codec_init
 * (0x07EC) follows the same rule at cold start, setting CPTEN last of all.
 *
 * The two values seen, 0xAC and 0xA8, differ only in bit 2; the same pair
 * appears at the Rev 20 call sites, so whatever bit 2 selects is unchanged.
 *
 * ---------------------------------------------------------------------------
 * REV 20 -> REV 22 DELTA.  Behaviour identical; the extracted block grew by
 * one instruction at the front.  Rev 20's version is 13 bytes at 0x0FF4 and
 * starts with a bare `MOVX @DPTR,A`, so its callers had to supply DPTR too:
 *
 *      rev20 0x034A / 0x0355:  MOV DPTR,#0xFFDE ; MOV A,#0xAC/#0xA8 ; LCALL 0x0FF4
 *      rev22 0x0356 / 0x035E:                     MOV A,#0xAC/#0xA8 ; LCALL 0x0FE2
 *
 * Rev 22's block opens with `MOV DPTR,#0xFFDE` itself: 16 bytes here, 3 saved
 * at each of the two call sites, net +16-13-6 = -3 bytes overall.  A source
 * function boundary would not move like this between revisions; a compiler
 * looking for the longest common instruction suffix would, because the code
 * around the call sites changed.  Read it as Keil's common-block extraction
 * pass (OPTIMIZE level 9).  The identical shift happens independently at
 * rev20 0x0E0F -> rev22 0x0EE8 in the ACG programming block, which is good
 * corroboration.  That inference is the best explanation of the two encodings;
 * it is not something the images state outright.
 *
 * The 13 bytes from 0x0FE5 onward are byte-identical to rev20 0x0FF4..0x0FF0
 * (`f0 90 ff d5 f0 90 ff b1 e0 44 01 f0 22`), verified by comparison.
 */
void cport_cnf3_write_enable(void) __naked {
    __asm
        mov   dptr,#0xffde         ; CPTCNF3  -- loaded here in Rev 22, by the
                                   ;   caller in Rev 20
        movx  @dptr,a              ; CPTCNF3 <- A (the caller's value)
        mov   dptr,#0xffd5         ; CPTRXCNF3
        movx  @dptr,a              ; same value to the receive-side config
        mov   dptr,#0xffb1         ; GLOBCTL
        movx  a,@dptr              ; read-modify-write: the other GLOBCTL bits
        orl   a,#0x01              ;   (12 MHz select, LPWR) must survive
        movx  @dptr,a              ; CPTEN = 1: codec port runs again
        ret
    __endasm;
}
